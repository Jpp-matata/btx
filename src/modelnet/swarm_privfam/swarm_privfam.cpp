// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_privfam/swarm_privfam.h>

#include <modelnet/swarm_privfam/offer.h>
#include <modelnet/swarm_privfam/publish.h>

namespace modelnet {

bool SwarmPrivFam::Offer(const std::string& family, bool private_flag, std::string& err)
{
    return SwarmPrivFamApply(m_roster, family, private_flag, err);
}

bool SwarmPrivFam::Publish(const std::string& family, std::string& err)
{
    return SwarmPrivFamPublish(m_shelf, m_roster, family, err);
}

bool SwarmPrivFam::AddSeed(const std::string& host, std::string& err)
{
    return m_seeds.Add(host, err);
}

bool SwarmPrivFam::offered(const std::string& family) const
{
    return m_roster.contains(family);
}

bool SwarmPrivFam::is_private(const std::string& family) const
{
    return m_roster.private_of(family);
}

std::size_t SwarmPrivFam::offered_count() const
{
    return m_roster.size();
}

bool SwarmPrivFam::published() const
{
    return m_shelf.published();
}

bool SwarmPrivFam::published(const std::string& family) const
{
    return m_shelf.published(family);
}

const std::vector<std::string>& SwarmPrivFam::dns_seeds() const
{
    return m_seeds.list();
}

bool SwarmPrivFam::connected() const
{
    return m_link.connected();
}

bool SwarmPrivFam::connected(const std::string& family) const
{
    return m_link.connected(family);
}

int64_t SwarmPrivFam::bytes_moved() const
{
    return m_quiet.bytes_moved();
}

bool SwarmPrivFam::launched() const
{
    return m_quiet.launched();
}

} // namespace modelnet
