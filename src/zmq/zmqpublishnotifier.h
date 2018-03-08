// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_ZMQ_ZMQPUBLISHNOTIFIER_H
#define MERIT_ZMQ_ZMQPUBLISHNOTIFIER_H

#include "zmqabstractnotifier.h"
#include "util.h"

class CBlockIndex;

class CZMQAbstractPublishNotifier : public CZMQAbstractNotifier
{
private:
    uint32_t nSequence; //!< upcounting per message sequence number

public:

    /* send zmq multipart message
       parts:
          * command
          * data
          * message sequence number
    */
    bool SendMessage(const char *command, const void* data, size_t size);

    bool Initialize(void *pcontext) override;
    void Shutdown() override;
};

class CZMQPublishHashBlockNotifier : public CZMQAbstractPublishNotifier
{
public:
    CZMQPublishHashBlockNotifier() {
        LogPrint(BCLog::ZMQ, "Starting Hash Block Notifier");
    };

    bool NotifyBlock(const CBlockIndex *pindex) override;
};

class CZMQPublishHashTransactionNotifier : public CZMQAbstractPublishNotifier
{
public:
    CZMQPublishHashTransactionNotifier() {
        LogPrint(BCLog::ZMQ, "Starting Hash Transaction Notifier");
    };

    bool NotifyTransaction(const CTransaction &transaction) override;
};

class CZMQPublishHashReferralNotifier : public CZMQAbstractPublishNotifier
{
public:
    CZMQPublishHashReferralNotifier() {
        LogPrint(BCLog::ZMQ, "Starting Hash Referral Notifier");
    };

    bool NotifyReferral(const referral::ReferralRef &ref) override;
};

class CZMQPublishRawBlockNotifier : public CZMQAbstractPublishNotifier
{
public:
    CZMQPublishRawBlockNotifier() {
        LogPrint(BCLog::ZMQ, "Starting Raw Block Notifier");
    };

    bool NotifyBlock(const CBlockIndex *pindex) override;
};

class CZMQPublishRawTransactionNotifier : public CZMQAbstractPublishNotifier
{
public:
    CZMQPublishRawTransactionNotifier() {
        LogPrint(BCLog::ZMQ, "Starting Raw Transaction Notifier");
    };

    bool NotifyTransaction(const CTransaction &transaction) override;
};

class CZMQPublishRawReferralNotifier : public CZMQAbstractPublishNotifier
{
public:
    CZMQPublishRawReferralNotifier() {
        LogPrint(BCLog::ZMQ, "Starting Raw Referral Notifier");
    };

    bool NotifyReferral(const referral::ReferralRef &ref) override;
};

#endif // MERIT_ZMQ_ZMQPUBLISHNOTIFIER_H
