// Concord
//
// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License"). You may not use this product except in
// compliance with the Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright notices and license terms. Your use of
// these subcomponents is subject to the terms and conditions of the sub-component's license, as noted in the LICENSE
// file.

#include "PreProcessRequestMsg.hpp"
#include "assertUtils.hpp"
#include <cstring>

namespace preprocessor {

PreProcessRequestMsg::PreProcessRequestMsg(NodeIdType senderId,
                                           uint16_t clientId,
                                           uint8_t sendReply,
                                           uint64_t reqSeqNum,
                                           ViewNum currentView,
                                           uint32_t reqLength,
                                           const char* request)
    : MessageBase(senderId, MsgCode::PreProcessRequest, (sizeof(PreProcessRequestMsgHeader) + reqLength)) {
  setParams(senderId, clientId, sendReply, reqSeqNum, currentView, reqLength);
  memcpy(body() + sizeof(PreProcessRequestMsgHeader), request, reqLength);
}

bool PreProcessRequestMsg::ToActualMsgType(MessageBase* inMsg, PreProcessRequestMsg*& outMsg) {
  Assert(inMsg->type() == MsgCode::PreProcessRequest);
  if (inMsg->size() < sizeof(PreProcessRequestMsgHeader)) return false;
  auto* msg = (PreProcessRequestMsg*)inMsg;
  if (msg->size() < (sizeof(PreProcessRequestMsgHeader) + msg->msgBody()->requestLength)) {
    LOG_ERROR(GL,
              "PreProcessRequestMsg size is too small: size="
                  << inMsg->size() << " and it is less than required requestLength=" << msg->msgBody()->requestLength
                  << " plus header size=" << sizeof(PreProcessRequestMsgHeader));
    return false;
  }
  outMsg = msg;
  LOG_DEBUG(GL,
            "senderId=" << outMsg->senderId() << " clientId=" << outMsg->clientId()
                        << " reqSeqNum=" << outMsg->reqSeqNum() << " view=" << outMsg->viewNum()
                        << " reqLength=" << outMsg->requestLength());
  return true;
}

void PreProcessRequestMsg::setParams(
    NodeIdType senderId, uint16_t clientId, uint8_t sendReply, ReqId reqSeqNum, ViewNum view, uint32_t reqLength) {
  msgBody()->senderId = senderId;
  msgBody()->clientId = clientId;
  msgBody()->sendReply = sendReply;
  msgBody()->reqSeqNum = reqSeqNum;
  msgBody()->viewNum = view;
  msgBody()->requestLength = reqLength;
  LOG_DEBUG(GL,
            "senderId=" << senderId << " clientId=" << clientId << " reqSeqNum=" << reqSeqNum << " view=" << view
                        << " reqLength=" << reqLength);
}

}  // namespace preprocessor
