/*
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * mod_sqs.c - Sends FreeSWITCH events to AWS SQS
 *
 * Copyright © 2026 Dextrous Technologies, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <switch.h>
#include <switch_utf8.h>
#include "sqs_helper.h"

/*** Module Configuration ***/
#define DEFAULT_QUEUE_SIZE 5000
#define DEFAULT_CIRCUIT_BREAKER_MS 10000
#define DEFAULT_NUM_THREADS 1

/*** SQS Queue Types ***/
enum sqs_queue_type {
	SQS_STANDARD,
	SQS_FIFO
};

/* Structure for event to keep name and subclass */
typedef struct {
	switch_event_types_t id;
	char* subclass;
} mod_sqs_events_t;

/*** Config Profile Types ***/
enum sqs_profile_type {
	EVENT_PROFILE,
	CDR_PROFILE
};

/* Structure to hold SQS profile configuration */
typedef struct {
	char *name;
	enum sqs_profile_type type;
	switch_queue_t *send_queue;
	enum sqs_queue_type queue_type;
	int queue_size;
	int circuit_breaker_ms;
	int num_threads;
	int parallel_submission;
	int running;
	mod_sqs_events_t events[SWITCH_EVENT_ALL];
	int event_count;
	switch_thread_t **threads;
	int active_threads;
	switch_memory_pool_t *pool;
	switch_time_t circuit_breaker_reset_time;
	aws_config_t *aws_params;
} mod_sqs_profile_t;

/* Structure for messages to be sent */
typedef struct {
	char *payload;
	char *unique_key;
	char *event_name;
} mod_sqs_message_t;

/* Globals */
static struct {
	switch_hash_t *profile_hash;
	switch_memory_pool_t *pool;
} mod_sqs_globals;

/*** Function Prototypes ***/
SWITCH_MODULE_LOAD_FUNCTION(mod_sqs_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_sqs_shutdown);
SWITCH_MODULE_DEFINITION(mod_sqs, mod_sqs_load, mod_sqs_shutdown, NULL);

static switch_status_t mod_sqs_profile_create(const char *name, switch_xml_t cfg);
static void *SWITCH_THREAD_FUNC mod_sqs_sender_thread(switch_thread_t *thread, void *data);
static void mod_sqs_event_handler(switch_event_t *evt);
static char *sanitize_msg(char *msg);
switch_status_t mod_sqs_cdr_handler(switch_core_session_t *session);

// State handler for receiving CDRs
static switch_state_handler_table_t state_handlers = {
	/*.on_init */ NULL,
	/*.on_routing */ NULL,
	/*.on_execute */ NULL,
	/*.on_hangup */ NULL,
	/*.on_exchange_media */ NULL,
	/*.on_soft_execute */ NULL,
	/*.on_consume_media */ NULL,
	/*.on_hibernate */ NULL,
	/*.on_reset */ NULL,
	/*.on_park */ NULL,
	/*.on_reporting */ mod_sqs_cdr_handler
};

void free_msg(mod_sqs_message_t *msg) {
	if (msg) {
		switch_safe_free(msg->payload);
		switch_safe_free(msg->event_name);
		switch_safe_free(msg->unique_key);
		switch_safe_free(msg);
	}
}

/*
 * SQS message bodies may only contain XML Char Unicode:
 *   #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD] | [#x10000-#x10FFFF]
 * Filters out most C0/C1 controls (except tab/LF/CR), surrogates, and U+FFFE/U+FFFF.
 */
static int sqs_char_allowed(unsigned int cp)
{
	return cp == 0x9 || cp == 0xA || cp == 0xD
		|| (cp >= 0x20 && cp <= 0xD7FF)
		|| (cp >= 0xE000 && cp <= 0xFFFD)
		|| (cp >= 0x10000 && cp <= 0x10FFFF);
}

/*
 * Strip invalid UTF-8 sequences and SQS-disallowed Unicode from msg in place.
 * Returns msg (NULL-safe). Message length only shrinks.
 */
static char* sanitize_msg(char *msg)
{
	unsigned char *src;
	unsigned char *dst;

	if (!msg) {
		return NULL;
	}

	src = (unsigned char *)msg;
	dst = src;

	while (*src) {
		//Step 1: drop all non utf-8 compliant bytes
		unsigned int candidate_character;
		int len = 0;

		// This is to figure out the character length from lead byte
		// Checks if source byte 1 is an ascii UTF-8 character | 0xxxxxxx
		if (src[0] <= 0x7F) { 
			candidate_character = src[0];
			len = 1;
		// Checks if 2 byte UTF-8 sequence is valid | 110xxxxx 10xxxxxx
		} else if ((src[0] & 0xE0) == 0xC0) {
			if (isutf(src[1])) {
				src++;
				while (*src && !isutf(*src))
					src++;
				continue;
			}
			candidate_character = ((src[0] & 0x1F) << 6) | (src[1] & 0x3F);
			if (candidate_character < 0x80) {
				src += 2;
				continue;
			}
			len = 2;
		// Checks if 3 byte UTF-8 sequence is valid | 1110xxxx 10xxxxxx 10xxxxxx
		} else if ((src[0] & 0xF0) == 0xE0) {
			// Continuations are !isutf (10xxxxxx). isutf here means ASCII/lead/NUL in a continuation slot which is not allowed
			if (isutf(src[1]) || isutf(src[2])) {
				src++;
				while (*src && !isutf(*src))
					src++;
				continue;
			}
			//Construct the candidate character
			candidate_character = ((src[0] & 0x0F) << 12) | ((src[1] & 0x3F) << 6) | (src[2] & 0x3F);
			//Validate the character
			if (candidate_character < 0x800 || (candidate_character >= 0xD800 && candidate_character <= 0xDFFF)) {
				src += 3;
				continue;
			}
			len = 3;
		// Checks if 4 byte UTF-8 sequence is valid | 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
		} else if ((src[0] & 0xF8) == 0xF0) {
			// Continuations are !isutf (10xxxxxx). isutf here means ASCII/lead/NUL in a continuation slot which is not allowed
			if (isutf(src[1]) || isutf(src[2]) || isutf(src[3])) {
				src++;
				while (*src && !isutf(*src))
					src++;
				continue;
			}
			//Construct the candidate character
			candidate_character = ((src[0] & 0x07) << 18) | ((src[1] & 0x3F) << 12)
				| ((src[2] & 0x3F) << 6) | (src[3] & 0x3F);
			//Validate the character
			if (candidate_character < 0x10000 || candidate_character > 0x10FFFF) {
				src += 4;
				continue;
			}
			len = 4;
		} else {
			// Invalid lead (e.g. lone continuation); skip it and any following continuations
			src++;
			while (*src && !isutf(*src))
				src++;
			continue;
		}

		//Step 2. Check if character is allowed by sqs
		if (sqs_char_allowed(candidate_character)) {
			while (len--) {
				*dst++ = *src++;
			}
		} else {
			src += len;
		}
	}

	*dst = '\0';
	return msg;
}

/*** Module Initialization ***/
switch_status_t mod_sqs_load(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool) {
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Loading mod_sqs...\n");

	memset(&mod_sqs_globals, 0, sizeof(mod_sqs_globals));
	mod_sqs_globals.pool = pool;
	switch_core_hash_init_nocase(&mod_sqs_globals.profile_hash);

	*module_interface = switch_loadable_module_create_module_interface(pool, "mod_sqs");

	/* Load profiles from XML configuration */
	switch_xml_t cfg, xml, profiles, profile;
	if (!(xml = switch_xml_open_cfg("sqs.conf", &cfg, NULL))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to open sqs.conf!\n");
		return SWITCH_STATUS_TERM;
	}

	if ((profiles = switch_xml_child(cfg, "profiles"))) {
		for (profile = switch_xml_child(profiles, "profile"); profile; profile = profile->next) {
			const char *name = switch_xml_attr_soft(profile, "name");
			if (!name) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Profile missing 'name' attribute\n");
				continue;
			}

			if (mod_sqs_profile_create(name, profile) != SWITCH_STATUS_SUCCESS) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create profile: %s\n", name);
				continue;
			}
		}
	}

	switch_xml_free(xml);

	initialize_aws_sdk();

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "mod_sqs loaded successfully.\n");
	return SWITCH_STATUS_SUCCESS;
}

/*** Module Shutdown ***/
switch_status_t mod_sqs_shutdown(void) {
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Shutting down mod_sqs...\n");

	/* Cleanup profiles */
	switch_hash_index_t *hi;
	for (hi = switch_core_hash_first(mod_sqs_globals.profile_hash); hi; hi = switch_core_hash_next(&hi)) {
		mod_sqs_profile_t *profile;
		switch_core_hash_this(hi, NULL, NULL, (void **)&profile);

		if (!profile) continue;

		if (profile->type == EVENT_PROFILE) {
			switch_event_unbind_callback(mod_sqs_event_handler);
		}

		if (profile->type == CDR_PROFILE) {
			switch_core_remove_state_handler(&state_handlers);
		}

		profile->running = 0;
		while (profile->active_threads > 0) {
			switch_yield(100000); // Yield CPU for 100ms
		}

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "All threads have terminated for profle '%s'\n", profile->name);
		switch_core_destroy_memory_pool(&profile->pool);
	}

	switch_core_hash_destroy(&mod_sqs_globals.profile_hash);
	shutdown_aws_sdk();

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "mod_sqs was shut down successfully.\n");
	return SWITCH_STATUS_SUCCESS;
}

/*** Profile Creation ***/
static switch_status_t mod_sqs_profile_create(const char *name, switch_xml_t cfg) {
	switch_memory_pool_t *pool;
	mod_sqs_profile_t *profile;

	if (switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS) {
		return SWITCH_STATUS_MEMERR;
	}

	profile = switch_core_alloc(pool, sizeof(mod_sqs_profile_t));
	profile->aws_params = switch_core_alloc(pool, sizeof(aws_config_t));
	profile->pool = pool;
	profile->name = switch_core_strdup(pool, name);
	profile->running = 1;

	/* Default configuration */
	profile->type = EVENT_PROFILE;
	profile->queue_size = DEFAULT_QUEUE_SIZE;
	profile->circuit_breaker_ms = DEFAULT_CIRCUIT_BREAKER_MS;
	profile->num_threads = DEFAULT_NUM_THREADS;
	profile->parallel_submission = 0;
	profile->event_count = 0;
	profile->queue_type = SQS_STANDARD;
	profile->active_threads = 0;

	/* Parse XML configuration */
	switch_xml_t param;
	for (param = switch_xml_child(cfg, "param"); param; param = param->next) {
		const char *var = switch_xml_attr(param, "name");
		const char *val = switch_xml_attr(param, "value");

		if (!var || !val) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Invalid parameter in profile: %s\n", name);
			continue;
		}

		if (!strcmp(var, "queue_url")) {
			profile->aws_params->queue_url = switch_core_strdup(pool, val);
		} else if (!strcmp(var, "access_key")) {
			profile->aws_params->access_key_id = switch_core_strdup(pool, val);
		} else if (!strcmp(var, "secret_key")) {
			profile->aws_params->secret_key = switch_core_strdup(pool, val);
		} else if (!strcmp(var, "internal_queue_size")) {
			profile->queue_size = atoi(val);
		} else if (!strcmp(var, "sqs_queue_type")) {
			if (!strcmp(val, "fifo")) {
				profile->queue_type = SQS_FIFO;
			} else {
				profile->queue_type = SQS_STANDARD;
			}
		} else if (!strcmp(var, "circuit_breaker_ms")) {
			profile->circuit_breaker_ms = atoi(val);
		} else if (!strcmp(var, "parallel_submission")) {
			profile->parallel_submission = switch_true(val);
		} else if (!strcmp(var, "num_threads")) {
			profile->num_threads = atoi(val);
		} else if (!strcmp(var, "type")) {
			if (!strcmp(val, "cdr")) {
				profile->type = CDR_PROFILE;
			} else {
				profile->type = EVENT_PROFILE;
			}
		} else if (!strcmp(var, "event_filter")) {
			char *event_str = switch_core_strdup(pool, val);
			char  *ev[SWITCH_EVENT_ALL];

			profile->event_count = switch_separate_string(event_str, ',', ev, (sizeof(ev) / sizeof(ev[0])));

			for (int i = 0; i < profile->event_count; i++) {
				char *subclass = SWITCH_EVENT_SUBCLASS_ANY;
				if ((subclass = strchr(ev[i], '^'))) {
					*subclass++ = '\0';
				}

				if (switch_name_event(ev[i], &(profile->events[i].id)) == SWITCH_STATUS_SUCCESS) {
					profile->events[i].subclass = subclass;
				} else {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "The switch event '%s' was not recognised!\n", ev[i]);
				}
			}
		}
	}

	/* Create message queue */
	if (switch_queue_create(&profile->send_queue, profile->queue_size, profile->pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create send queue for profile: %s\n", name);
		switch_core_destroy_memory_pool(&pool);
		return SWITCH_STATUS_GENERR;
	}

	// Create sender threads
	if (!profile->parallel_submission) profile->num_threads = 1;
	profile->threads = switch_core_alloc(pool, sizeof(switch_thread_t *) * profile->num_threads);
	switch_threadattr_t *thd_attr = NULL;
	switch_threadattr_create(&thd_attr, profile->pool);
	// Mark thread as detached, so it is cleaned up (joined) upon termination automatically
	switch_threadattr_detach_set(thd_attr, 1);

	for (int i = 0; i < profile->num_threads; i++) {
		if (switch_thread_create(&profile->threads[i], thd_attr, mod_sqs_sender_thread, profile, profile->pool) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create sender thread for profile: %s\n", name);
			profile->running = 0;
			switch_core_destroy_memory_pool(&pool);
			return SWITCH_STATUS_GENERR;
		}
	}

	if (profile->type == EVENT_PROFILE) {
		/* Bind events */
		for (int i = 0; i < profile->event_count; i++) {
			if (switch_event_bind_removable("mod_sqs", profile->events[i].id, profile->events[i].subclass,
				mod_sqs_event_handler, profile, NULL) != SWITCH_STATUS_SUCCESS) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to bind event handler for profile %s\n", profile->name);
				profile->running = 0;
				return SWITCH_STATUS_GENERR;
			}
		}
	} else if (profile->type == CDR_PROFILE) {
		switch_core_add_state_handler(&state_handlers);
	}

	switch_core_hash_insert(mod_sqs_globals.profile_hash, name, profile);
	return SWITCH_STATUS_SUCCESS;
}

/*** Sender Thread ***/
// this thread will try to retrieve messages out of the internal in-memory queue, and send them to Amazon SQS
static void *SWITCH_THREAD_FUNC mod_sqs_sender_thread(switch_thread_t *thread, void *data) {
	mod_sqs_profile_t *profile = (mod_sqs_profile_t *)data;
	mod_sqs_message_t *msg = NULL;
	sqs_message_t sqs_msg = {0};
	char* err = NULL;

	if (!profile) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to load the profile, terminating sender thread...\n");
		return NULL;
	}

	// Atomically increment active thread count for the profile
	__sync_fetch_and_add(&profile->active_threads, 1);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sender thread #%d started for profile '%s'\n", profile->active_threads, profile->name);

	while (profile->running) {
		// wait 1s for a message to show up
		if (switch_queue_pop_timeout(profile->send_queue, (void **)&msg, 1000000) != SWITCH_STATUS_SUCCESS) {
			continue;
		}

		if (msg) {
			sqs_msg.body = msg->payload;

			if (profile->queue_type == SQS_FIFO) {
				asprintf(&sqs_msg.message_group_id, "%s_%s", profile->name, msg->event_name);
				asprintf(&sqs_msg.message_deduplication_id, "%s_%s", msg->event_name, msg->unique_key);
			}

			int result = send_message_to_sqs(profile->aws_params, &sqs_msg, &err);

			if (result != 0) {
				if (err) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to send message to SQS! error: '%s'\n", err);
					switch_safe_free(err);
				} else {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to send message to SQS! No errors reported.\n");
				}

				if (switch_queue_trypush(profile->send_queue, msg) != SWITCH_STATUS_SUCCESS) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Queue full (profile '%s'), Could not re-queue the message. Message is lost!\n", profile->name);
					free_msg(msg);
				}
			} else {
				//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "'%s' Event sent to SQS successfully.\n", msg->event_name);
				if (profile->type == CDR_PROFILE) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "CDR for '%s' was sent to SQS successfully.\n", msg->unique_key);
				}
				free_msg(msg);
			}

			if (sqs_msg.message_group_id) {
				switch_safe_free(sqs_msg.message_group_id);
			}
			if (sqs_msg.message_deduplication_id) {
				switch_safe_free(sqs_msg.message_deduplication_id);
			}
		}
	}

	// Atomically decrement active thread count for the profile
	__sync_fetch_and_sub(&profile->active_threads, 1);
	return NULL;
}

/*** Event Handler ***/
// This will capture the event and push it to the internal in-memory queue
static void mod_sqs_event_handler(switch_event_t *evt) {
	mod_sqs_profile_t *profile = (mod_sqs_profile_t *)evt->bind_user_data;
	switch_time_t now = switch_time_now();
	switch_time_t reset_time;

	if (!profile || !profile->running) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Event received but profile '%s' is not running\n", profile->name);
		return;
	}

	/* If the circuit breaker is active, ignore the event */
	reset_time = profile->circuit_breaker_reset_time;
	if (now < reset_time) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Profile '%s': circuit breaker hit[%d] (%d)\n", profile->name, (int) now, (int) reset_time);
		return;
	}

	mod_sqs_message_t *msg;
	switch_zmalloc(msg, sizeof(mod_sqs_message_t));
	switch_event_serialize_json(evt, &msg->payload);
	sanitize_msg(msg->payload);
	switch_strdup(msg->event_name, switch_event_get_header(evt, "Event-Name"));
	switch_strdup(msg->unique_key, switch_event_get_header(evt, "Event-Sequence"));
	switch_tolower_max(msg->event_name);

    // Check if size of the serialized event is over 256KB, and reject it if it is as SQS does not support that size.
	size_t size_in_bytes = strlen(msg->payload);
	if (size_in_bytes > 1048576) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Message payload larger than 1MB, dropping message!\n");
		free_msg(msg);
	}

	if (switch_queue_trypush(profile->send_queue, msg) != SWITCH_STATUS_SUCCESS) {
		profile->circuit_breaker_reset_time = now + profile->circuit_breaker_ms * 1000;
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Queue full, dropping message!\n");
		free_msg(msg);
	}
}

switch_status_t mod_sqs_cdr_handler(switch_core_session_t *session) {
	cJSON *json_cdr = NULL;
	mod_sqs_message_t *msg = NULL;
	switch_hash_index_t *hi;
	switch_bool_t skip_by_filter = SWITCH_FALSE;
	switch_time_t now = switch_time_now();
	switch_time_t reset_time;

	switch_zmalloc(msg, sizeof(mod_sqs_message_t));
    
	if (switch_ivr_generate_json_cdr(session, &json_cdr, SWITCH_FALSE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error generating JSON CDR!\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_strdup(msg->payload, cJSON_PrintUnformatted(json_cdr));
	sanitize_msg(msg->payload);

	// Check if size of the serialized event is over 256KB, and reject it if it is as SQS does not support that size.
	size_t size_in_bytes = strlen(msg->payload);
	if (size_in_bytes > 1048576) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "CDR payload larger than 1MB, dropping message!\n");
		goto cleanup;
	}

	// send message to the queue of all CDR profiles
	for (hi = switch_core_hash_first(mod_sqs_globals.profile_hash); hi; hi = switch_core_hash_next(&hi)) {
		mod_sqs_profile_t *profile;
		switch_core_hash_this(hi, NULL, NULL, (void **)&profile);

		if (!profile || profile->type != CDR_PROFILE) continue;

		/* If the circuit breaker is active, ignore the event */
		reset_time = profile->circuit_breaker_reset_time;
		if (now < reset_time) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Profile '%s': circuit breaker hit[%d] (%d)\n", profile->name, (int) now, (int) reset_time);
			continue;
		}

		switch_strdup(msg->event_name, "cdr");
		switch_strdup(msg->unique_key, switch_core_session_get_uuid(session));

		if (switch_queue_trypush(profile->send_queue, msg) != SWITCH_STATUS_SUCCESS) {
			profile->circuit_breaker_reset_time = now + profile->circuit_breaker_ms * 1000;
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Queue full (profile '%s'), dropping message!\n", profile->name);
		}
	}

cleanup:
	if (json_cdr) {
		cJSON_Delete(json_cdr);
	}
	return SWITCH_STATUS_SUCCESS;
}
