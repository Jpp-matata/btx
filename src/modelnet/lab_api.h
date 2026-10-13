// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_LAB_API_H
#define BITCOIN_MODELNET_LAB_API_H

#include <string>

namespace modelnet {

/** Dispatch one `lab.<operation>` method. Unknown names return false with BTX035_NOT_IMPLEMENTED. */
bool DispatchLabApi(const std::string& method, const std::string& request_json, std::string& response_json, std::string& err);

/** Drop process-local lab state. Tests call this between cases. */
void ResetLabApi();

} // namespace modelnet

#endif // BITCOIN_MODELNET_LAB_API_H
