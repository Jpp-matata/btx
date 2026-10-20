// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_evalboard/board.h>

#include <modelnet/swarm_evalboard/check.h>
#include <modelnet/swarm_evalboard/quorum.h>

namespace modelnet {

bool SwarmEvalBoard::Submit(const std::string& subject, const std::string& author, const std::string& domain,
                            int64_t score, bool independent, bool rotating, std::string& err)
{
    if (!SwarmEvalAdmitReport(subject, author, domain, score, independent, rotating, err)) {
        return false;
    }
    SwarmEvalReport row;
    row.subject = subject;
    row.author = author;
    row.domain = domain;
    row.score = score;
    row.independent = independent;
    row.rotating = rotating;
    return m_ledger.Store(row, err);
}

bool SwarmEvalBoard::accepted(const std::string& subject) const
{
    return SwarmEvalAccepted(m_ledger.scores(subject));
}

std::vector<SwarmEvalReport> SwarmEvalBoard::scores(const std::string& subject) const
{
    return m_ledger.scores(subject);
}

bool SwarmEvalBoard::Attach(const std::string& subject, std::string& err)
{
    return m_attach.Attach(subject, accepted(subject), err);
}

bool SwarmEvalBoard::Regress(const std::string& subject, std::string& err)
{
    // The ledger is not touched. Stored scores stay stored.
    return m_attach.Regress(subject, err);
}

bool SwarmEvalBoard::attached(const std::string& subject) const
{
    return m_attach.attached(subject);
}

bool SwarmEvalBoard::regressed(const std::string& subject) const
{
    return m_attach.regressed(subject);
}

bool SwarmEvalBoard::Graduate(const std::string& subject, std::string& err)
{
    return m_realm.Graduate(subject, err);
}

} // namespace modelnet
