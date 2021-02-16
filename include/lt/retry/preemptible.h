#pragma once

#include "lt/retry/retry-policy.h"
#include "lt/retry/policies.h"

#include <condition_variable>

namespace lt { namespace retry {

class PreemptibleRetryStatus : public RetryStatus
{
   public:
    PreemptibleRetryStatus() : RetryStatus(), condition_signalled() {}

    PreemptibleRetryStatus(RetryStatus retry_status, bool signalled)
        : RetryStatus(retry_status), condition_signalled(signalled)
    {}

    bool condition_signalled;
};

// Switch from one RetryPolicy to another when a condition becomes true.
// While using the first RetryPolicy the delay can be preempted by the
// condition allowing the action to be immediately re-attempted.
//
// Shared state:
// ```
// std::condition_variable cv;
// std::mutex cv_m;
// bool condition = false;
// ```
//
// Retry thread:
// ```
//    auto policy_before = ...; // eg. constantDelay(100ms);
//    auto policy_after = ...; // eg. constantDelay(10ms);
//    auto policy = PreemptibleRetry(policy_before, policy_after);
//
//    auto shouldRetry = ...;
//    auto action = ...;
//    auto predicate = [&condition]() { return condition; }
//
//    policy.retry(cv, cv_m, predicate, shouldRetry, action);
// ```
//
// The condition must only be modified while holding the mutex, after
// which we can notify any retry operations which are waiting on it:
// ```
//    {
//        std::lock_guard<std::mutex> lock(cv_m);
//        condition = true;
//    }
//    cv.notify_all();
// ```

class PreemptibleRetry
{
   private:
    RetryPolicy policy_before_;
    RetryPolicy policy_after_;

   public:
    explicit PreemptibleRetry(RetryPolicy policy_before, RetryPolicy policy_after)
        : policy_before_(std::move(policy_before)),
          policy_after_(std::move(policy_after))
    {
    }

    std::optional<PreemptibleRetryStatus> applyAndPreemptibleDelay(
        std::condition_variable& cv,
        std::mutex& cv_mutex,
        std::function<bool()> cond,
        PreemptibleRetryStatus status0) const
    {
        std::unique_lock<std::mutex> lock(cv_mutex);
        if (cond()) {
            lock.unlock();

            // If the condition was signalled in the previous retry, reset
            // the retry status, as these values are used independently by
            // each policy. For example, if we retry connecting 1000 times
            // before connecting, we don't want the subsequent exponential
            // backoff to start at 2^1000
            if (status0.condition_signalled) {
                status0 = {};
            }
            auto ostatus = policy_after_.applyAndDelay(status0);
            return ostatus ?
                std::make_optional(PreemptibleRetryStatus(*ostatus, false)) :
                std::nullopt;
        }

        auto ostatus = policy_before_.apply(status0);

        if (!ostatus) {
            return std::nullopt;
        }

        auto status = *ostatus;

        if (status.previous_delay) {
            if(cv.wait_for(lock, *status.previous_delay, cond)) {
                // Condition met
                return PreemptibleRetryStatus(status, true);
            }
        }

        return PreemptibleRetryStatus(status, false);
    }

    template <typename T>
    T retry(
        std::condition_variable& cv,
        std::mutex& cv_mutex,
        std::function<bool()> cond,
        std::function<bool(PreemptibleRetryStatus, T)> shouldRetry,
        std::function<T(PreemptibleRetryStatus)> action) const
    {
        PreemptibleRetryStatus status{};

        while (true) {
            auto result = action(status);

            if (!shouldRetry(status, result)) {
                return result;
            }

            auto new_status = applyAndPreemptibleDelay(cv, cv_mutex, cond, status);

            if (!new_status) {
                return result;
            }

            status = *new_status;
        }
    }

    std::vector<PreemptibleRetryStatus> simulate(int n_before, int n_after) const
    {
        RetryStatus status{};
        std::vector<PreemptibleRetryStatus> xs;

        for (int i = 0; i < n_before; i++) {
            auto new_status = policy_before_.apply(status);

            if (!new_status) {
                return xs;
            }

            status = *new_status;
            xs.push_back(PreemptibleRetryStatus(status, false));
        }

        // Reset the status after the condition is signalled
        status = {};

        for (int i = 0; i < n_after; i++) {
            auto new_status = policy_after_.apply(status);

            if (!new_status) {
                return xs;
            }

            status = *new_status;
            xs.push_back(PreemptibleRetryStatus(status, true));
        }

        return xs;
    }
};

}}  // namespace lt::retry
