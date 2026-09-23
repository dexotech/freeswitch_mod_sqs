#ifndef SQS_HELPER_H
#define SQS_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AwsConfig {
	char* queue_url;
	char* access_key_id;
	char* secret_key;
} aws_config_t;

typedef struct SqsMessage {
	char* body;
	char* message_group_id;
	char* message_deduplication_id;
} sqs_message_t;

void initialize_aws_sdk();
void shutdown_aws_sdk();
int send_message_to_sqs(const aws_config_t* config, const sqs_message_t* msg, char** error_message);

#ifdef __cplusplus
}
#endif

#endif // SQS_HELPER_H
