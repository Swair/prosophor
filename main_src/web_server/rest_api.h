// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include <httplib.h>

#include <nlohmann/json.hpp>

#include "prosophor_core/public/config.h"
#include "web_server/group_store.h"
#include "web_server/user_store.h"

namespace prosophor {

/// 注册全部 /api/* REST 路由。
/// 除 /api/auth/register|login 外均需 `Authorization: Bearer <token>`。
/// @param sessions_dir 会话历史根目录({BaseDir}/sessions),历史读取用。
void RegisterRestApi(httplib::Server& svr, UserStore& users, GroupStore& groups,
                     const WebConfig& config, const std::string& sessions_dir);

// ── JSON 响应辅助(httplib 无内建 JSON 支持)──

/// 统一成功响应:JSON 体 + 状态码。
void JsonResponse(httplib::Response& res, const nlohmann::json& body, int status = 200);

/// 统一错误体:{"error":{"code":..., "message":...}}
void JsonError(httplib::Response& res, int status, const std::string& code,
               const std::string& message);

/// Bearer 认证:失败时已写 401 响应并返回 nullopt。
std::optional<WebUser> Authenticate(const httplib::Request& req, httplib::Response& res,
                                    const UserStore& users);

}  // namespace prosophor
