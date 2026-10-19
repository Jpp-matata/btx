// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_mandate.h>

#include <univalue.h>

#include <fstream>
#include <iterator>

namespace modelnet {
namespace {

bool EnsureDir(const fs::path& modeldir, fs::path& swarm_dir, std::string& err)
{
    if (modeldir.empty()) {
        err = "MODELDIR";
        return false;
    }
    swarm_dir = modeldir / "swarm";
    fs::create_directories(swarm_dir);
    if (!fs::exists(swarm_dir)) {
        err = "SWARM_DIR";
        return false;
    }
    return true;
}

bool WriteAtomic(const fs::path& final_path, const std::string& body, std::string& err)
{
    const fs::path tmp = fs::u8path(final_path.utf8string() + ".tmp");
    {
        std::ofstream file(tmp, std::ios::trunc);
        if (!file) {
            err = "WRITE";
            return false;
        }
        file << body;
    }
    std::error_code ec;
    fs::rename(tmp, final_path, ec);
    if (ec) {
        err = "RENAME";
        return false;
    }
    err.clear();
    return true;
}

bool ReadRaw(const fs::path& path, std::string& raw)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool SameIdentity(const SwarmMandate& a, const SwarmMandate& b)
{
    return a.mandate_id == b.mandate_id && a.pool_id == b.pool_id && a.actor == b.actor && a.recipients == b.recipients &&
           a.ceiling == b.ceiling && a.expiry_height == b.expiry_height && a.nonce == b.nonce;
}

} // namespace

bool SwarmMandateBook::Open(int64_t collateral, int64_t exit_reserve, int64_t fee_reserve, std::string& err)
{
    if (m_open) {
        err = "REOPEN";
        return false;
    }
    if (collateral <= 0 || exit_reserve < 0 || fee_reserve < 0 || exit_reserve + fee_reserve >= collateral) {
        err = "COLLATERAL";
        return false;
    }
    m_open = true;
    m_collateral = collateral;
    m_exit = exit_reserve;
    m_fee = fee_reserve;
    m_mandates.clear();
    m_work.clear();
    err.clear();
    return true;
}

bool SwarmMandateBook::Grant(const SwarmMandate& mandate, std::string& err)
{
    if (!m_open || mandate.mandate_id.empty() || mandate.pool_id.empty() || mandate.actor.empty() || mandate.nonce.empty() ||
        mandate.recipients.empty() || mandate.ceiling <= 0 || mandate.expiry_height <= 0) {
        err = "MANDATE";
        return false;
    }
    const auto it = m_mandates.find(mandate.mandate_id);
    if (it != m_mandates.end()) {
        if (!SameIdentity(it->second, mandate)) {
            err = "CONFLICT";
            return false;
        }
        err.clear();
        return true;
    }
    SwarmMandate stored = mandate;
    stored.revoked = false;
    stored.spent = 0;
    m_mandates.emplace(stored.mandate_id, std::move(stored));
    err.clear();
    return true;
}

bool SwarmMandateBook::Revoke(const std::string& mandate_id, std::string& err)
{
    const auto it = m_mandates.find(mandate_id);
    if (!m_open || it == m_mandates.end()) {
        err = "MANDATE";
        return false;
    }
    it->second.revoked = true;
    err.clear();
    return true;
}

bool SwarmMandateBook::Reserve(const std::string& mandate_id, const std::string& work_id, const std::string& recipient,
                               int64_t atoms, int64_t height, std::string& err)
{
    if (!m_open || work_id.empty()) {
        err = "WORK";
        return false;
    }
    const auto it = m_mandates.find(mandate_id);
    if (it == m_mandates.end()) {
        err = "MANDATE";
        return false;
    }
    if (it->second.revoked) {
        err = "REVOKED";
        return false;
    }
    if (height >= it->second.expiry_height) {
        err = "EXPIRED";
        return false;
    }
    bool listed = false;
    for (const std::string& allowed : it->second.recipients) {
        if (allowed == recipient) listed = true;
    }
    if (!listed) {
        err = "RECIPIENT";
        return false;
    }
    if (atoms <= 0) {
        err = "AMOUNT";
        return false;
    }
    const auto work = m_work.find(work_id);
    if (work != m_work.end()) {
        if (work->second.mandate_id == mandate_id && work->second.recipient == recipient && work->second.atoms == atoms) {
            err.clear();
            return true;
        }
        err = "CONFLICT";
        return false;
    }
    if (it->second.spent > it->second.ceiling - atoms) {
        err = "CEILING";
        return false;
    }
    const SwarmConservation now = Conserved();
    if (atoms > now.available) {
        err = "CONSERVATION";
        return false;
    }
    it->second.spent += atoms;
    m_work.emplace(work_id, Work{mandate_id, recipient, atoms});
    err.clear();
    return true;
}

SwarmConservation SwarmMandateBook::Conserved() const
{
    SwarmConservation out;
    out.collateral = m_collateral;
    out.exit_reserve = m_exit;
    out.fee_reserve = m_fee;
    out.inflight = 0;
    for (const auto& item : m_mandates) out.assigned += item.second.spent;
    out.available = m_collateral - m_exit - m_fee - out.inflight - out.assigned;
    out.ok = m_open && out.available >= 0 &&
             out.available + out.inflight + out.assigned + out.exit_reserve + out.fee_reserve == m_collateral;
    return out;
}

bool SwarmMandateBook::Persist(const fs::path& modeldir, std::string& err)
{
    if (!m_open) {
        err = "NOT_OPEN";
        return false;
    }
    fs::path swarm_dir;
    if (!EnsureDir(modeldir, swarm_dir, err)) return false;
    UniValue mandates(UniValue::VARR);
    for (const auto& item : m_mandates) {
        const SwarmMandate& mandate = item.second;
        UniValue recipients(UniValue::VARR);
        for (const std::string& recipient : mandate.recipients) recipients.push_back(recipient);
        UniValue row(UniValue::VOBJ);
        row.pushKV("actor", mandate.actor);
        row.pushKV("ceiling", mandate.ceiling);
        row.pushKV("expiry_height", mandate.expiry_height);
        row.pushKV("mandate_id", mandate.mandate_id);
        row.pushKV("nonce", mandate.nonce);
        row.pushKV("pool_id", mandate.pool_id);
        row.pushKV("recipients", recipients);
        row.pushKV("revoked", mandate.revoked);
        row.pushKV("spent", mandate.spent);
        mandates.push_back(std::move(row));
    }
    UniValue work(UniValue::VARR);
    for (const auto& item : m_work) {
        UniValue row(UniValue::VOBJ);
        row.pushKV("atoms", item.second.atoms);
        row.pushKV("mandate_id", item.second.mandate_id);
        row.pushKV("recipient", item.second.recipient);
        row.pushKV("work_id", item.first);
        work.push_back(std::move(row));
    }
    UniValue out(UniValue::VOBJ);
    out.pushKV("automatic_spend_atoms", "0");
    out.pushKV("collateral", m_collateral);
    out.pushKV("exit_reserve", m_exit);
    out.pushKV("fee_reserve", m_fee);
    out.pushKV("mandates", mandates);
    out.pushKV("work", work);
    return WriteAtomic(swarm_dir / "mandates.json", out.write(0, 0) + "\n", err);
}

bool SwarmMandateBook::Load(const fs::path& modeldir, std::string& err)
{
    std::string raw;
    UniValue root;
    if (!ReadRaw(modeldir / "swarm" / "mandates.json", raw) || !root.read(raw) || !root.isObject()) {
        err = "MANDATE_READ";
        return false;
    }
    if (root.exists("automatic_spend_atoms") && root["automatic_spend_atoms"].get_str() != "0") {
        err = "SPEND";
        return false;
    }
    m_collateral = root["collateral"].getInt<int64_t>();
    m_exit = root["exit_reserve"].getInt<int64_t>();
    m_fee = root["fee_reserve"].getInt<int64_t>();
    m_mandates.clear();
    m_work.clear();
    for (const UniValue& item : root["mandates"].getValues()) {
        SwarmMandate mandate;
        mandate.actor = item["actor"].get_str();
        mandate.ceiling = item["ceiling"].getInt<int64_t>();
        mandate.expiry_height = item["expiry_height"].getInt<int64_t>();
        mandate.mandate_id = item["mandate_id"].get_str();
        mandate.nonce = item["nonce"].get_str();
        mandate.pool_id = item["pool_id"].get_str();
        mandate.revoked = item["revoked"].get_bool();
        mandate.spent = item["spent"].getInt<int64_t>();
        for (const UniValue& recipient : item["recipients"].getValues()) mandate.recipients.push_back(recipient.get_str());
        m_mandates.emplace(mandate.mandate_id, std::move(mandate));
    }
    for (const UniValue& item : root["work"].getValues()) {
        Work row;
        row.atoms = item["atoms"].getInt<int64_t>();
        row.mandate_id = item["mandate_id"].get_str();
        row.recipient = item["recipient"].get_str();
        m_work.emplace(item["work_id"].get_str(), std::move(row));
    }
    m_open = true;
    err.clear();
    return true;
}

} // namespace modelnet
