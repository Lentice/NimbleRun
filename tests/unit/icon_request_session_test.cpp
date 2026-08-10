#include "test_util.h"

#include "app_host/icon_request_session.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using nimblerun::IconRequestSession;

void TestPendingKeyIsRequestedOnce() {
    IconRequestSession session;
    Expect(session.ShouldRequest(L"app|48", false), "a new key can be requested");
    session.BeginRequest(L"app|48");
    Expect(!session.ShouldRequest(L"app|48", false),
           "a key in flight is not requested twice");
}

void TestFailureWaitsForNextShow() {
    IconRequestSession session;
    session.BeginRequest(L"failed|48");
    session.OnResult(L"failed|48", false);
    Expect(!session.ShouldRequest(L"failed|48", false),
           "a failed key is not retried in the same session");
    session.OnShow();
    Expect(session.ShouldRequest(L"failed|48", false),
           "a failed key is retryable on the next show");
}

void TestCachedKeyIsNotRequested() {
    IconRequestSession session;
    Expect(!session.ShouldRequest(L"cached|48", true),
           "a cached key never starts a worker request");
}

void TestDroppedKeyCanBeRequestedAgain() {
    IconRequestSession session;
    session.BeginRequest(L"dropped|48");
    session.DrainDropped({L"dropped|48"});
    Expect(session.ShouldRequest(L"dropped|48", false),
           "draining a dropped key clears its pending state");
}

void TestShowDoesNotClearPending() {
    IconRequestSession session;
    session.BeginRequest(L"in-flight|48");
    session.OnShow();
    Expect(!session.ShouldRequest(L"in-flight|48", false),
           "show leaves an in-flight key pending");
}

} // namespace

int wmain() {
    TestPendingKeyIsRequestedOnce();
    TestFailureWaitsForNextShow();
    TestCachedKeyIsNotRequested();
    TestDroppedKeyCanBeRequestedAgain();
    TestShowDoesNotClearPending();
    std::puts("NR-138 icon request session check PASSED");
    return 0;
}
