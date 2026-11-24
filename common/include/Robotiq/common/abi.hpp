// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief ROBOTIQ_COMMON_ABI names the inline namespace that versions the
//! common/ symbols, so SDKs built from different versions link side by side.

#pragma once

#ifndef ROBOTIQ_COMMON_ABI
#error "ROBOTIQ_COMMON_ABI is undefined: link the module through robotiq_link_common() or its _headers target"
#endif
