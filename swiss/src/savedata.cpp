/* -*- mode: C; c-basic-offset: 4; intent-tabs-mode: nil -*-
 *
 * swiss - your Sifteo utility knife
 *
 * Copyright <c> 2012 Sifteo, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "savedata.h"
#include "basedevice.h"
#include "lfsvolume.h"
#include "util.h"
#include "bits.h"
#include "metadata.h"
#include "progressbar.h"
#include "swisserror.h"

#include <string>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>

using namespace std;

const char * SaveData::SYSLFS_PACKAGE_STR = "com.sifteo.syslfs";

int SaveData::run(int argc, char **argv, IODevice &_dev)
{
    SaveData saveData(_dev);

    if (!_dev.open(IODevice::SIFTEO_VID, IODevice::BASE_PID)) {
        return ENODEV;
    }

    if (argc >= 4 && !strcmp(argv[1], "extract")) {

        char *path = 0;
        char *pkgStr = 0;
        bool raw = false;
        bool rpc = false;

        for (int i = 2; i < argc; ++i) {
            if (!strcmp(argv[i], "--rpc")) {
                rpc = true;
            } else if (!strcmp(argv[i], "--raw")) {
                raw = true;
            } else if (!pkgStr) {
                pkgStr = argv[i];
            } else {
                path = argv[i];
            }
        }

        if (!path || !pkgStr) {
            fprintf(stderr, "incorrect args\n");
            return EINVAL;
        }

        return saveData.extract(pkgStr, path, raw, rpc);
    }

    if (argc >= 3 && !strcmp(argv[1], "restore")) {
        const char *path = argv[2];
        return saveData.restore(path);
    }

    if (argc >= 4 && !strcmp(argv[1], "normalize")) {
        const char *inpath = argv[2];
        const char *outpath = argv[3];
        return saveData.normalize(inpath, outpath);
    }

    if (argc >= 3 && !strcmp(argv[1], "delete")) {
        const char *pkgStr = argv[2];
        return saveData.del(pkgStr);
    }

    fprintf(stderr, "incorrect args\n");
    return EINVAL;
}

SaveData::SaveData(IODevice &_dev) :
    dev(_dev)
{}

int SaveData::extract(const char *pkgStr, const char *filepath, bool raw, bool rpc)
{
    /*
     * Retrieve all LFS volumes for a given parent volume.
     *
     * Concatenate their data into the file @ filepath.
     *
     * We don't do any parsing of the data at this point.
     */

    BaseDevice base(dev);

    unsigned volume;
    if (!base.volumeCodeForPackage(std::string(pkgStr), volume)) {
        fprintf(stderr, "can't extract data from %s: not installed\n", pkgStr);
        return EINVAL;
    }

    USBProtocolMsg buf;

    UsbVolumeManager::LFSDetailReply *reply = base.getLFSDetail(buf, volume);
    if (!reply) {
        return EIO;
    }

    if (reply->count == 0) {
        printf("no savedata found for %s\n", pkgStr);
        return EOK;
    }

    /*
     * If a raw file has been requested, write the raw filesystem data there
     * and be done.
     *
     * Otherwise, write the raw data to a tmp file, then feed it as
     * the input to normalize(), and remove it when we're done.
     */

    //TODO: This isn't secure and needs corrected
    const char *rawfilepath = raw ? filepath : tmpnam(0);
    //const char *rawfilepath = raw ? filepath : mkstemp(0);

    FILE *fraw = fopen(rawfilepath, "wb");
    if (!fraw) {
        fprintf(stderr, "couldn't open %s: %s\n", filepath, strerror(errno));
        return ENOENT;
    }

    if (!writeFileHeader(fraw, volume, reply->count)) {
        return EIO;
    }

    if (!writeVolumes(reply, fraw, rpc)) {
        return EIO;
    }

    if (raw) {
        return EOK;
    }

    fclose(fraw);
    int rv = normalize(rawfilepath, filepath);
    remove(rawfilepath);

    printf("complete - see tools/savedata.py within your SDK installation "
           "to interpret the contents of %s\n", filepath);

    return rv;
}


int SaveData::restore(const char *filepath)
{
    /*
     * please implement me. thank you in advance.
     */

    FILE *fin = fopen(filepath, "rb");
    if (!fin) {
        fprintf(stderr, "couldn't open %s: %s\n", filepath, strerror(errno));
        return ENOENT;
    }

    int fileVersion;
    if (!getValidFileVersion(fin, fileVersion)) {
        fclose(fin);
        return EINVAL;
    }

    HeaderCommon hdr;
    if (!readHeader(fileVersion, hdr, fin)) {
        fclose(fin);
        return EINVAL;
    }

    BaseDevice base(dev);
    unsigned volBlockCode;
    if (!base.volumeCodeForPackage(hdr.packageStr, volBlockCode)) {
        fprintf(stderr, "can't restore: %s in not installed\n", hdr.packageStr.c_str());
        return ENOENT;
    }

    Records records;
    if (!retrieveRecords(records, hdr, fin)) {
        fclose(fin);
        return EIO;
    }

    if (!restoreRecords(volBlockCode, records)) {
        return EIO;
    }

    return EOK;
}

int SaveData::normalize(const char *inpath, const char *outpath)
{
    FILE *fin = fopen(inpath, "rb");
    if (!fin) {
        fprintf(stderr, "couldn't open %s: %s\n", inpath, strerror(errno));
        return ENOENT;
    }

    FILE *fout = fopen(outpath, "wb");
    if (!fout) {
        fprintf(stderr, "couldn't open %s: %s\n", outpath, strerror(errno));
        return ENOENT;
    }

    int fileVersion;
    if (!getValidFileVersion(fin, fileVersion)) {
        fclose(fin);
        return EINVAL;
    }

    HeaderCommon hdr;
    if (!readHeader(fileVersion, hdr, fin)) {
        fclose(fin);
        return EINVAL;
    }

    Records records;
    if (!retrieveRecords(records, hdr, fin)) {
        fclose(fin);
        return EINVAL;
    }

    if (!writeNormalizedRecords(records, hdr, fout)) {
        return EIO;
    }

    return EOK;
}


int SaveData::del(const char *pkgStr)
{
    /*
     * Delete any save data belonging to the specified package.
     */

    BaseDevice base(dev);

    unsigned volume;
    if (!base.volumeCodeForPackage(std::string(pkgStr), volume)) {
        fprintf(stderr, "can't extract data from %s: not installed\n", pkgStr);
        return EINVAL;
    }

    USBProtocolMsg m(USBProtocol::Installer);
    m.header |= UsbVolumeManager::DeleteLFSChildren;
    m.append((uint8_t*) &volume, sizeof volume);

    if (!base.writeAndWaitForReply(m)) {
        return EIO;
    }

    return EOK;
}


/********************************************************
 * Internal
 ********************************************************/


bool SaveData::restoreRecords(unsigned vol, const Records &records)
{
    ScopedProgressBar pb(records.size());
    unsigned progress = 0;

    for (Records::const_iterator it = records.begin(); it != records.end(); it++) {

        const std::vector<Record> &recs = it->second;

        // only restore the most recent instance of this key's payload
        if (!recs.empty()) {
            if (!restoreItem(vol, recs.back())) {
                return false;
            }
        }

        progress++;
        pb.update(progress);
    }

    while (dev.numPendingOUTPackets())
        dev.processEvents(1);

    return true;
}

bool SaveData::restoreItem(unsigned parentVol, const Record & record)
{
    /*
     * Restore a single key-value pair.
     */

    BaseDevice base(dev);

    USBProtocolMsg m;
    if (!base.beginLFSRestore(m, parentVol, record.key, record.payload.size(), record.crc)) {
        return false;
    }

    unsigned progress = 0;

    while (progress < record.payload.size()) {
        m.init(USBProtocol::Installer);
        m.header |= UsbVolumeManager::WriteLFSObjectPayload;

        unsigned chunk = std::min(unsigned(record.payload.size() - progress), m.bytesFree());
        ASSERT(chunk != 0);

        m.append(&record.payload[progress], chunk);
        progress += chunk;

        if (dev.writePacket(m.bytes, m.len) < 0) {
            return false;
        }
        while (dev.numPendingOUTPackets() > IODevice::MAX_OUTSTANDING_OUT_TRANSFERS)
            dev.processEvents(1);
    }

    return true;
}


bool SaveData::getValidFileVersion(FILE *f, int &version)
{
    /*
     * Retrieve this file's version and ensure it's valid.
     */

    struct MiniHeader {
        uint64_t    magic;
        uint32_t    version;
    } minihdr;

    if (fread(&minihdr, sizeof minihdr, 1, f) != 1) {
        fprintf(stderr, "i/o error: %s\n", strerror(errno));
        return false;
    }

    if (minihdr.magic != MAGIC) {
        fprintf(stderr, "not a recognized savedata file\n");
        return false;
    }

    rewind(f);
    version = minihdr.version;

    switch (version) {
    case 0x1:
        fprintf(stderr, "savedata file version 0x1 detected,"
                "there are known errors with this version, "
                "and this data cannot be restored\n");
        return false;

    case 0x2:
        return true;

    default:
        fprintf(stderr, "unsupported savedata file version: 0x%x\n", minihdr.version);
        return false;
    }
}


bool SaveData::readHeader(int version, HeaderCommon &h, FILE *f)
{
    /*
     * Given the version, convert this file's header into HeaderCommon.
     */

    if (version == 2) {
        HeaderV2 v2;
        if (fread(&v2, sizeof v2, 1, f) != 1) {
            fprintf(stderr, "i/o error: %s\n", strerror(errno));
            return false;
        }

        h.numBlocks     = v2.numBlocks;
        h.mc_blockSize  = v2.mc_blockSize;
        h.mc_pageSize   = v2.mc_pageSize;

        memcpy(h.appUUID.bytes, v2.appUUID.bytes, sizeof(h.appUUID.bytes));
        memcpy(h.baseUniqueID, v2.baseUniqueID, sizeof(h.baseUniqueID));

        if (!readStr(h.baseFirmwareVersionStr, f) ||
            !readStr(h.packageStr, f) ||
            !readStr(h.versionStr, f))
        {
            return false;
        }

        return true;
    }

    return false;
}


bool SaveData::retrieveRecords(Records &records, const HeaderCommon &details, FILE *f)
{
    /*
     * Common implementation for all savedata file versions.
     */

    for (unsigned b = 0; b < details.numBlocks; ++b) {

        LFSVolume volume(details.mc_pageSize, details.mc_blockSize);
        if (!volume.init(f)) {
            fprintf(stderr, "couldn't init volume, skipping\n");
            continue;
        }

        volume.retrieveRecords(records);
    }

    return true;
}


void SaveData::writeNormalizedItem(stringstream & ss, uint8_t key, uint32_t len, const void *data)
{
    /*
     * Each header item in a simplified savedata file looks like:
     * <uint8_t key> <uint32_t bloblen> <bloblen bytes of payload>
     */

    ss << key;
    ss.write((const char*)&len, sizeof(len));
    ss.write((const char*)data, len);
}


bool SaveData::writeNormalizedRecords(Records &records, const HeaderCommon &details, FILE *f)
{
    uint64_t magic = NORMALIZED_MAGIC;
    if (fwrite(&magic, sizeof magic, 1, f) != 1) {
        return false;
    }

    /*
     * Write headers section
     */

    stringstream hs;
    writeNormalizedItem(hs, PackageString, details.packageStr.length(), details.packageStr.c_str());
    writeNormalizedItem(hs, VersionString, details.versionStr.length(), details.versionStr.c_str());
    writeNormalizedItem(hs, UUID, sizeof(details.appUUID), &details.appUUID);
    writeNormalizedItem(hs, BaseHWID, sizeof(details.baseUniqueID), &details.baseUniqueID);
    writeNormalizedItem(hs, BaseFirmwareVersion, details.baseFirmwareVersionStr.length(), details.baseFirmwareVersionStr.c_str());

    struct NormalizedSectionHeader {
        uint32_t type;
        uint32_t length;
    } section;

    section.type = SectionHeader;
    section.length = hs.tellp();

    if (fwrite(&section, sizeof section, 1, f) != 1) {
        return false;
    }

    if (fwrite(hs.str().c_str(), section.length, 1, f) != 1) {
        return false;
    }

    /*
     * Write records section
     */

    stringstream rs;
    for (Records::const_iterator it = records.begin(); it != records.end(); it++) {

        const std::vector<Record> &recs = it->second;

        for (std::vector<Record>::const_iterator r = recs.begin(); r != recs.end(); r++) {
            writeNormalizedItem(rs, r->key, r->payload.size(), &(r->payload[0]));
        }
    }

    section.type = SectionRecords;
    section.length = rs.tellp();

    if (fwrite(&section, sizeof section, 1, f) != 1) {
        return false;
    }

    if (fwrite(rs.str().c_str(), section.length, 1, f) != 1) {
        return false;
    }

    return true;
}


bool SaveData::writeFileHeader(FILE *f, unsigned volBlockCode, unsigned numVolumes)
{
    /*
     * Simple header that describes the contents of our savedata file.
     * Subset of the info specified in a Backup.
     */

    HeaderV2 hdr = {
        MAGIC,          // magic
        VERSION,        // version
        numVolumes,     // numVolumes
        PAGE_SIZE,      // mc_pageSize
        BLOCK_SIZE,     // mc_blockSize,
    };

    BaseDevice base(dev);
    USBProtocolMsg m;
    const UsbVolumeManager::SysInfoReply *sysinfo = base.getBaseSysInfo(m);
    if (!sysinfo) {
        return false;
    }

    memcpy(hdr.baseUniqueID, sysinfo->baseUniqueID, sizeof(hdr.baseUniqueID));
    hdr.baseHwRevision = sysinfo->baseHwRevision;

    USBProtocolMsg mFWV;
    const char *fwv = base.getFirmwareVersion(mFWV);
    if (!fwv) {
        return false;
    }
    string baseFWVersion(fwv);

    Metadata metadata(dev);
    std::string packageID = metadata.getString(volBlockCode, _SYS_METADATA_PACKAGE_STR);
    std::string version = metadata.getString(volBlockCode, _SYS_METADATA_VERSION_STR);
    int uuidLen = metadata.getBytes(volBlockCode, _SYS_METADATA_UUID, hdr.appUUID.bytes, sizeof hdr.appUUID.bytes);

    if (volBlockCode == SYSLFS_VOLUME_BLOCK_CODE) {
        packageID = SYSLFS_PACKAGE_STR;
    } else {
        if (packageID.empty() || version.empty() || uuidLen <= 0) {
            return false;
        }
    }

    if (fwrite(&hdr, sizeof hdr, 1, f) != 1)
        return false;

    return writeStr(baseFWVersion, f) && writeStr(packageID, f) && writeStr(version, f);
}


bool SaveData::writeVolumes(UsbVolumeManager::LFSDetailReply *reply, FILE *f, bool rpc)
{
    ScopedProgressBar pb(reply->count * BLOCK_SIZE);
    unsigned progTotal = reply->count * BLOCK_SIZE;
    unsigned overallProgress = 0;

    /*
     * For each block, request all of its data, and write it out to our file.
     */

    for (unsigned i = 0; i < reply->count; ++i) {

        unsigned baseAddress = reply->records[i].address;
        unsigned requestProgress = 0, replyProgress = 0;

        // Queue up the first few reads, respond as results arrive
        for (unsigned b = 0; b < 3; ++b) {
            sendRequest(baseAddress, requestProgress);
        }

        while (replyProgress < BLOCK_SIZE) {
            dev.processEvents(1);

            while (dev.numPendingINPackets() != 0) {
                if (!writeReply(f, replyProgress)) {
                    return false;
                }

                pb.update(overallProgress + replyProgress);
                if (rpc) {
                    fprintf(stdout, "::progress:%u:%u\n", overallProgress + replyProgress, progTotal); fflush(stdout);
                }
                sendRequest(baseAddress, requestProgress);
            }
        }
        overallProgress += BLOCK_SIZE;
    }

    return true;
}


bool SaveData::sendRequest(unsigned baseAddr, unsigned & progress)
{
    /*
     * Request another chunk of data, maintaining progress within
     * the current block.
     */

    unsigned offset = progress;
    if (offset >= BLOCK_SIZE) {
        return false;
    }

    USBProtocolMsg m(USBProtocol::Installer);
    m.header |= UsbVolumeManager::FlashDeviceRead;
    UsbVolumeManager::FlashDeviceReadRequest *req =
        m.zeroCopyAppend<UsbVolumeManager::FlashDeviceReadRequest>();

    req->address = baseAddr + offset;
    req->length = std::min(BLOCK_SIZE - offset,
        USBProtocolMsg::MAX_LEN - USBProtocolMsg::HEADER_BYTES);

    progress = offset + req->length;

    dev.writePacket(m.bytes, m.len);

    return true;
}


bool SaveData::writeReply(FILE *f, unsigned & progress)
{
    /*
     * Read the pending reply from the device and write it out to our file.
     */

    USBProtocolMsg m(USBProtocol::Installer);
    BaseDevice base(dev);

    uint32_t header = m.header | UsbVolumeManager::FlashDeviceRead;
    if (!base.waitForReply(header, m)) {
        return false;
    }

    unsigned len = m.payloadLen();
    progress += len;

    return fwrite(m.castPayload<uint8_t>(), len, 1, f) == 1;
}


bool SaveData::writeStr(const std::string &s, FILE *f)
{
    uint32_t length = s.length();

    if (fwrite(&length, sizeof length, 1, f) != 1)
        return false;

    if (fwrite(s.c_str(), length, 1, f) != 1)
        return false;

    return true;
}


bool SaveData::readStr(std::string &s, FILE *f)
{
    /*
     * strings are preceded by their uint32_t length.
     * a little stupid to allocate and free the intermediate buffer, but
     * handy to be able to return a std::string.
     */

    uint32_t length;

    if (fread(&length, sizeof length, 1, f) != 1) {
        return false;
    }

    char *buf = (char*)malloc(length);
    if (!buf) {
        return false;
    }

    if (fread(buf, length, 1, f) != 1) {
        return false;
    }

    s.assign(buf, length);

    // trim any junk on the end
    while (s.at(s.length() - 1) == '\0') {
        s.erase(s.length() - 1);
    }

    free(buf);

    return true;
}
