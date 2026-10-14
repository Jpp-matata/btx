// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_LAB_DISK_H
#define BITCOIN_MODELNET_LAB_DISK_H

#include <modelnet/research_views.h>
#include <modelnet/training_integer.h>
#include <util/fs.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace modelnet {

/** One stored exchange object. Identity secret keys are not part of this image. */
struct LabDiskObject {
    std::string kind;
    std::string digest;
    std::string content;
};

/**
 * Fields a second lab process needs. automatic_spend_atoms is always "0" on
 * disk. Secret identity keys are not represented and are never written.
 */
struct LabDiskImage {
    std::map<std::string, std::string> records;
    std::vector<ResearchObservation> observations;
    std::map<std::string, std::vector<LabDiskObject>> repo_objects;
    std::set<std::string> repos;
    std::set<std::string> snapshots;
    std::map<std::string, std::string> realms;
    std::map<std::string, std::string> lineage;
    std::map<std::string, std::string> plans;
    std::map<std::string, std::string> admissions;
    /** Public identity keys, lowercase hex. Never a secret key. */
    std::map<std::string, std::string> identity_pk;
    /** INTEGER_LINEAR trainer state keyed by trajectory id. No secret keys. */
    std::map<std::string, IntegerState> trains;
    /** Trajectory id to the public identity that ran it. Absent for an older file. */
    std::map<std::string, std::string> train_authors;
};

/** BTX035_LAB_DIR, or empty when unset. No relative or home fallback. */
fs::path LabDiskDirectory();

/**
 * Load lab.json when the directory and file exist. A missing directory or file
 * leaves `image` unchanged. Does not create the directory.
 */
bool LoadLabDisk(LabDiskImage& image, std::string& err);

/**
 * Write lab.json by temp file then rename. A failed call leaves any previous
 * lab.json in place. Does nothing when BTX035_LAB_DIR is unset.
 */
bool SaveLabDisk(const LabDiskImage& image, std::string& err);

} // namespace modelnet

#endif // BITCOIN_MODELNET_LAB_DISK_H
