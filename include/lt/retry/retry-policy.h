#pragma once

#include <iostream>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

namespace lt { namespace retry {

struct RetryStatus
{
    int iteration_number;
    std::chrono::microseconds cumulative_delay;
    std::optional<std::chrono::microseconds> previous_delay;
};

inline std::ostream &operator<<(std::ostream &stream, const RetryStatus &status)
{
    stream << "{ iteration_number: " << status.iteration_number
           << ", cumulative_delay: " << status.cumulative_delay.count() << "us";

    if (status.previous_delay) {
        stream << ", previous_delay: " << status.previous_delay->count() << "us";
    } else {
        stream << ", previous_delay: none";
    }

    return stream << " }";
}

class RetryPolicy
{
   private:
    std::function<std::optional<std::chrono::microseconds>(RetryStatus)> _policy;

   public:
    explicit RetryPolicy(std::function<std::optional<std::chrono::microseconds>(RetryStatus)> policy)
        : _policy(std::move(policy))
    {
    }

    std::optional<std::chrono::microseconds> operator()(RetryStatus status) const
    {
        return _policy(status);
    }

    std::optional<RetryStatus> apply(RetryStatus status) const
    {
        auto odelay = _policy(status);

        if (!odelay) {
            return std::nullopt;
        }

        auto delay = *odelay;

        status.iteration_number = status.iteration_number + 1;
        status.cumulative_delay = status.cumulative_delay + delay;
        status.previous_delay = delay;

        return status;
    }

    std::optional<RetryStatus> applyAndDelay(RetryStatus status0) const
    {
        auto ostatus = apply(status0);

        if (!ostatus) {
            return std::nullopt;
        }

        auto status = *ostatus;

        if (status.previous_delay) {
            std::this_thread::sleep_for(*status.previous_delay);
        }

        return status;
    }

    friend RetryPolicy operator+(RetryPolicy x, RetryPolicy y)
    {
        return RetryPolicy([=](RetryStatus status) -> std::optional<std::chrono::microseconds> {
            auto xresult = x._policy(status);
            auto yresult = y._policy(status);

            if (xresult && yresult) {
                return std::max(*xresult, *yresult);
            }

            return std::nullopt;
        });
    }

    template <typename T>
    T retry(std::function<bool(RetryStatus, T)> shouldRetry, std::function<T(RetryStatus)> action) const
    {
        RetryStatus status{};

        while (true) {
            auto result = action(status);

            if (!shouldRetry(status, result)) {
                return result;
            }

            auto new_status = applyAndDelay(status);

            if (!new_status) {
                return result;
            }

            status = *new_status;
        }
    }

    std::vector<RetryStatus> simulate(int n) const
    {
        RetryStatus status{};
        std::vector<RetryStatus> xs;

        for (int i = 0; i < n; i++) {
            auto new_status = apply(status);

            if (!new_status) {
                return xs;
            }

            status = *new_status;
            xs.push_back(status);
        }

        return xs;
    }
};

}}  // namespace lt::retry
