# ptp_c - Generic PTP Implementation

`ptp_c` is a simplified implementation of the IEEE1588v2 protocol. The goal is an implementation that is "as generic as possible", meaning that this library neither implements the timestamping, nor uses basic functions like "sleep".

This means that functions that are not necessarily "part of the protocol" but are still essential and might be platform-dependent will be passed in as a function pointer by the user.
Currently this includes the following functions:

- `sleep_ms`: A function to let the calling thread sleep, with milliseconds as the parameter. Reason being that various platforms use completely different functions for this, microcontrollers especially.
- `mutex_lock`, `mutex_unlock` (+ the type `mutex_t`): Again differs on various platforms
- `get_timestamp_ns` (for Rx and Tx respectively): While being essential to PTP (that's the whole point basically), the decision was made to make the implementation of this function up to the user as there're lots of different ways a project might fetch these timestamps. This way the user can have the same function for both Rx / Tx or seperate ones and they can either get them via their PTP-enabled PHY for the highest accuracy or just refer to a software timestamp. Doesn't matter.
- `set_time_ns` and `set_time_offset_ns`: Setting the time is project dependent. Another possibility would be storing the "current time" in the PTP clock instance as a variable, but currently there's no benefit in doing that. So currently the new time will get passed back to the user directly.
- `send` and `receive`: Also platform dependent **and** transport layer dependent (Layer2 or IP), so I figured to let this be up to the user as the PTP protocol itself doesn't differ between those two transport layers.

## NOTE

Currently this library only supports acting as an ordinary clock. Acting as a grandmaster clock is planned (although probably without utilizing the best master clock algorithm). 


