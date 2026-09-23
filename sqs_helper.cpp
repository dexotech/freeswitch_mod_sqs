/*
 * sqs_helper.cpp - stitches the FS module to AWS SQS SDK
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

#include <aws/core/Aws.h>
#include <aws/sqs/SQSClient.h>
#include <aws/sqs/model/SendMessageRequest.h>
#include <aws/sqs/model/SendMessageResult.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <iostream>
#include <cstring>
#include <mutex>

// Thread-safe initialization and shutdown
static std::once_flag sdk_init_flag;
static std::once_flag sdk_shutdown_flag;

// Configuration struct for AWS details
struct AwsConfig {
	char* queue_url;
	char* access_key_id;
	char* secret_key;
};

struct SqsMessage {
	char* body;
	char* message_group_id;
	char* message_deduplication_id;
};

// Initialize the AWS SDK (thread-safe)
extern "C" void initialize_aws_sdk() {
	std::call_once(sdk_init_flag, []() {
		Aws::SDKOptions options;
		Aws::InitAPI(options);
	});
}

// Shutdown the AWS SDK (thread-safe)
extern "C" void shutdown_aws_sdk() {
	std::call_once(sdk_shutdown_flag, []() {
		Aws::SDKOptions options;
		Aws::ShutdownAPI(options);
	});
}

// Send a message to SQS (thread-safe)
extern "C" int send_message_to_sqs(const AwsConfig* config, const SqsMessage* msg, char** error_message = nullptr) {
	Aws::Client::ClientConfiguration clientConfig;

	Aws::Auth::AWSCredentials credentials;
	credentials.SetAWSAccessKeyId(Aws::String(config->access_key_id));
	credentials.SetAWSSecretKey(Aws::String(config->secret_key));

	Aws::SQS::SQSClient sqs(credentials, clientConfig);

	Aws::SQS::Model::SendMessageRequest request;
	request.SetQueueUrl(config->queue_url);
	request.SetMessageBody(msg->body);

	if (msg->message_group_id != nullptr) {
		request.SetMessageGroupId(msg->message_group_id);
	}

	if (msg->message_deduplication_id != nullptr) {
		request.SetMessageDeduplicationId(msg->message_deduplication_id);
	}

	auto outcome = sqs.SendMessage(request);

	if (!outcome.IsSuccess()) {
		if (error_message) {
			*error_message = strdup(outcome.GetError().GetMessage().c_str());
		}
		return -1;
	}

	return 0;
}
