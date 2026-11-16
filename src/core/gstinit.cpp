// SPDX-License-Identifier: GPL-3.0-or-later
#include "gstinit.h"

#include <gst/gst.h>

#include <mutex>

namespace mmm::gst {

void ensureInitialized()
{
    static std::once_flag once;
    std::call_once(once, [] { gst_init(nullptr, nullptr); });
}

} // namespace mmm::gst
