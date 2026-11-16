// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mmm::gst {

// Initialise GStreamer once per process. Thread-safe; cheap after the first call.
void ensureInitialized();

} // namespace mmm::gst
