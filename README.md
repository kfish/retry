lt::retry
=========

Overview
--------

`lt::retry` provides a number of combinators for building up retry
policies that allow for an action to be retried when it fails.

Usage
-----

```cpp
// Create a policy which can retry up to 10 times and initially sleeps
// for 10ms. After each failed retry the sleep time is doubled.
lt::retry::RetryPolicy policy =
    lt::retry::exponentialBackoff(std::chrono::milliseconds(10)) +
    lt::retry::limitRetries(10);

enum class Result
{
    FAILED_TRY_AGAIN,
    FAILED_FATAL_GIVE_UP,
    SUCCESS
};

auto shouldRetry = [](lt::retry::RetryStatus _, Result result) -> bool {
    return result == Result::FAILED_TRY_AGAIN;
};

auto action = [&](retry::RetryStatus _) -> Result {
    //
    // do stuff
    //

    if (okToRetry) {
        return Result::FAILED_TRY_AGAIN;
    }

    if (fatal) {
        return Result::FAILED_FATAL_GIVE_UP;
    }

    return Result::SUCCESS;
};

auto result = policy.retry<Result>(shouldRetry, action);

// result == Result::FAILED_TRY_AGAIN if we ran out of retries
```
