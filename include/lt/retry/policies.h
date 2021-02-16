#pragma once

#include "lt/retry/retry-policy.h"

#include <cmath>
#include <random>

namespace lt { namespace retry {

//
// Never retry
//
inline RetryPolicy neverRetry()
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> { return std::nullopt; });
}

//
// Retry immediately, but only up to 'retryLimit' times.
//
inline RetryPolicy limitRetries(int retryLimit)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        if (status.iteration_number >= retryLimit) return std::nullopt;

        return std::chrono::microseconds(0);
    });
}

//
// Set a limit on the total time spent retrying
//
inline RetryPolicy limitCumulativeDelay(std::chrono::microseconds cumulativeDelayLimit, RetryPolicy policy)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        auto delay = policy(status);

        if (delay && (*delay + status.cumulative_delay) >= cumulativeDelayLimit) {
            return std::nullopt;
        }

        return delay;
    });
}

//
// Set an absolute time point beyond which to stop retrying,
// eg. don't retry after 07:02:00 AM today
//
inline RetryPolicy limitTimePoint(std::chrono::system_clock::time_point time_point_limit, RetryPolicy policy)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        auto delay = policy(status);

        if (delay && (*delay + std::chrono::system_clock::now()) > time_point_limit) {
            return std::nullopt;
        }

        return policy(status);
    });
}

//
// Set a delay limit on a policy such that once the given delay amount has been
// reached or exceeded, the policy will stop retrying.
//
inline RetryPolicy limitRetriesByDelay(std::chrono::microseconds delayLimit, RetryPolicy policy)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        auto delay = policy(status);

        if (delay && *delay >= delayLimit) {
            return std::nullopt;
        }

        return delay;
    });
}

//
// Constant delay with unlimited retries.
//
inline RetryPolicy constantDelay(std::chrono::microseconds delay)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> { return delay; });
}

//
// Full jitter delay with unlimited retries.
//
inline RetryPolicy fullJitter(std::chrono::microseconds max_delay)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        static thread_local std::mt19937 generator;
        std::uniform_int_distribution<int> distribution(0, max_delay.count());

        return std::chrono::microseconds(distribution(generator));
    });
}

//
// Equal jitter delay with unlimited retries.
//
inline RetryPolicy equalJitter(std::chrono::microseconds max_delay)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        auto half_n = max_delay / 2;

        static thread_local std::mt19937 generator;
        std::uniform_int_distribution<int> distribution(0, half_n.count());

        return half_n + std::chrono::microseconds(distribution(generator));
    });
}


//
// Grow delay exponentially each iteration. Each delay will increase by a
// factor of two.
//
inline RetryPolicy exponentialBackoff(std::chrono::microseconds base)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        return base * static_cast<int>(std::pow(2, status.iteration_number));
    });
}

//
// Full jitter exponential backoff as explained in the AWS Architecture
// article.
//
// http://www.awsarchitectureblog.com/2015/03/backoff.html
//
// NB. this function provides the uncapped "Full jitter backoff" delay. Typically
// you would combine this with a finite limit such as a cap on the delay:
//
//     auto policy = capDelay(1000ms, fullJitterBackoff(10us));
//
inline RetryPolicy fullJitterBackoff(std::chrono::microseconds base)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        auto n = (base * static_cast<int>(std::pow(2, status.iteration_number)));

        static thread_local std::mt19937 generator;
        std::uniform_int_distribution<int> distribution(0, n.count());

        return std::chrono::microseconds(distribution(generator));
    });
}

//
// Equal jitter exponential backoff as explained in the AWS Architecture
// article.
//
// http://www.awsarchitectureblog.com/2015/03/backoff.html
//
// NB. this function provides the uncapped "Equal jitter backoff" delay. Typically
// you would combine this with a finite limit such as a cap on the delay:
//
//     auto policy = capDelay(1000ms, equalJitterBackoff(10us));
//
inline RetryPolicy equalJitterBackoff(std::chrono::microseconds base)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        auto half_n = (base * static_cast<int>(std::pow(2, status.iteration_number))) / 2;

        static thread_local std::mt19937 generator;
        std::uniform_int_distribution<int> distribution(0, half_n.count());

        return half_n + std::chrono::microseconds(distribution(generator));
    });
}

//
// Decorrelated jitter backoff as explained in the AWS Architecture
// article.
//
// http://www.awsarchitectureblog.com/2015/03/backoff.html
//
// NB. this function provides the uncapped "Decorrelated jitter backoff" delay. Typically
// you would combine this with a finite limit such as a cap on the delay:
//
//     auto policy = capDelay(1000ms, decorrelatedJitterBackoff(10us));
//
inline RetryPolicy decorrelatedJitterBackoff(std::chrono::microseconds base)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        if (status.previous_delay) {
            auto prev = *status.previous_delay;

            static thread_local std::mt19937 generator;
            std::uniform_int_distribution<int> distribution(0, prev.count() * 3);

            return std::chrono::microseconds(distribution(generator));
        } else {
            return std::nullopt;
        }
    });
}

//
// Set an upper bound on the delay for a policy.
//
// For example, `capDelay(1000us, exponentialBackoff(10us))` will never sleep
// for longer than 1000us.
//
inline RetryPolicy capDelay(std::chrono::microseconds maxDelay, RetryPolicy policy)
{
    return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
        auto delay = policy(status);

        if (delay) {
            return std::min(maxDelay, *delay);
        }

        return std::nullopt;
    });
}

}}  // namespace lt::retry
