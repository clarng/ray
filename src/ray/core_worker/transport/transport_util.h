// Copyright 2023 The Ray Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "ray/gcs/pb_util.h"

namespace ray {
namespace core {

inline std::optional<rpc::RayErrorInfo> GetErrorInfoFromGetWorkerFailureCauseReply(
    const rpc::WorkerAddress &addr,
    const Status &get_worker_failure_cause_reply_status,
    const rpc::GetWorkerFailureCauseReply &get_worker_failure_cause_reply) {
  if (get_worker_failure_cause_reply_status.ok()) {
    RAY_LOG(DEBUG) << "Worker failure for " << addr.worker_id << ": "
                   << ray::gcs::RayErrorInfoToString(
                          get_worker_failure_cause_reply.failure_cause());
    if (get_worker_failure_cause_reply.has_failure_cause()) {
      return std::make_optional(get_worker_failure_cause_reply.failure_cause());
    } else {
      return std::nullopt;
    }
  } else {
    RAY_LOG(DEBUG) << "Failed to fetch worker failure with status "
                   << get_worker_failure_cause_reply_status.ToString()
                   << " node id: " << addr.raylet_id << " ip: " << addr.ip_address;
    std::stringstream buffer;
    buffer << "Worker failed due to the node dying.\n\nThe node (IP: " << addr.ip_address
           << ", node ID: " << addr.raylet_id
           << ") where this worker was running crashed unexpectedly. "
           << "This can happen if: (1) the instance where the node was running failed, "
              "(2) raylet crashes unexpectedly (OOM, preempted node, etc).\n\n"
           << "To see more information about the crash, use `ray logs raylet.out -ip "
           << addr.ip_address << "`";
    rpc::RayErrorInfo error_info;
    error_info.set_error_message(buffer.str());
    error_info.set_error_type(rpc::ErrorType::NODE_DIED);
    return std::make_optional(error_info);
  }
}

}  // namespace core
}  // namespace ray
