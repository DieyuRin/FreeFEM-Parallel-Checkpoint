// FreeFemCheckpoint
// Parallel checkpoint/restart plugin for FreeFEM.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// This project is an independent third-party project and is not part of
// the official FreeFEM distribution.
//
// FreeFemCheckpoint.cpp
// FreeFEM parallel checkpoint/restart plugin (V3 stabilization, 0.3.0).
//
// Public API (collective on checkpointComm() = MPI_COMM_WORLD):
//   ffcpWrite(filename, gid, values)   -- canonical partition-independent write
//   ffcpRead (filename, gid, values)   -- canonical N-to-M read
// Legacy aliases of the same implementations:
//   ffioWriteGlobal / ffioReadGlobal
//
// Canonical layout: one value per global FE DOF id (supplied by FreeFEM's
// restrict()).  File format FFIOG001: 64-byte header + IEEE-754 double
// payload; the payload does not depend on the writer MPI partition.
//
// Error codes: see docs/API.md (exact table generated from the constants
// below).  All application-level validation failures are collectively agreed
// before any rank returns, so no rank can strand others in a collective.
// MPI-level failures are checked and reported; recovery from arbitrary MPI
// faults is not claimed.

#include "ff++.hpp"
#include <mpi.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace std;

namespace {

// =====================================================================
// Format constants and compile-time guards
// =====================================================================
static const char    kMagic[8]     = {'F','F','I','O','G','0','0','1'};
constexpr long long  kHeaderSize   = 64;
constexpr uint64_t   kFormatVersion = 1;

static_assert(sizeof(double) == 8, "FFIOG001 requires 64-bit double");
static_assert(sizeof(uint64_t) == 8, "FFIOG001 requires uint64_t");
static_assert(std::numeric_limits<double>::is_iec559,
              "FFIOG001 expects IEEE-754 double");

// =====================================================================
// Public error codes (negative long returned from the FreeFEM functions;
// a positive return is the number of canonical DOFs written/read).
// =====================================================================
constexpr long kErrInvalidArgument  = -1;
constexpr long kErrSizeMismatch     = -2;
constexpr long kErrLocalTooLarge    = -3;
constexpr long kErrOpen             = -4;
constexpr long kErrHeader           = -5;
constexpr long kErrMagic            = -6;
constexpr long kErrVersion          = -7;
constexpr long kErrScalarWidth      = -8;
constexpr long kErrEmpty            = -9;
constexpr long kErrGidRange         = -10;
constexpr long kErrBlockTooLarge    = -11;
constexpr long kErrTruncated        = -12;
constexpr long kErrTrailingBytes    = -13;
constexpr long kErrRead             = -14;
constexpr long kErrWrite            = -15;
constexpr long kErrInvalidGid       = -16;
constexpr long kErrCanonicalization = -17;
constexpr long kErrOverflow         = -18;
constexpr long kErrNonFinite        = -19;
constexpr long kErrComm             = -20;

// Duplicated overlap DOFs must agree to this relative tolerance; larger
// mismatches indicate an inconsistent distributed state or wrong gid map.
// Non-finite values (NaN/Inf) are rejected outright.
constexpr double kDuplicateTolerance = 1.0e-12;

// =====================================================================
// Communicator + collective validation helpers
// =====================================================================
MPI_Comm checkpointComm() { return MPI_COMM_WORLD; }

// All ranks must agree before any rank returns from a collective routine.
bool collectiveAnyError(int localError, MPI_Comm comm)
{
    int globalError = 0;
    MPI_Allreduce(&localError, &globalError, 1, MPI_INT, MPI_MAX, comm);
    return globalError != 0;
}

// Reduce a local error *code* to a single rank-invariant code: every rank
// returns the most severe (most negative) code observed anywhere.
long checkpointFail(int localCode, MPI_Comm comm)
{
    int lc = -localCode, gc = 0;
    MPI_Allreduce(&lc, &gc, 1, MPI_INT, MPI_MAX, comm);
    return static_cast<long>(-gc);
}

void printFailure(long code)
{
    // called on rank 0 only
    switch (code) {
    case kErrInvalidArgument:  cerr << "[FreeFemCheckpoint] ERROR: invalid argument (null filename/gid/values)" << endl; break;
    case kErrSizeMismatch:     cerr << "[FreeFemCheckpoint] ERROR: gid[] and values[] sizes differ" << endl; break;
    case kErrLocalTooLarge:    cerr << "[FreeFemCheckpoint] ERROR: a local array exceeds INT_MAX" << endl; break;
    case kErrOpen:             cerr << "[FreeFemCheckpoint] ERROR: cannot open checkpoint file" << endl; break;
    case kErrHeader:           cerr << "[FreeFemCheckpoint] ERROR: truncated/malformed 64-byte header" << endl; break;
    case kErrMagic:            cerr << "[FreeFemCheckpoint] ERROR: not a FFIOG001 checkpoint" << endl; break;
    case kErrVersion:          cerr << "[FreeFemCheckpoint] ERROR: unsupported format version" << endl; break;
    case kErrScalarWidth:      cerr << "[FreeFemCheckpoint] ERROR: unsupported scalar width (double only)" << endl; break;
    case kErrEmpty:            cerr << "[FreeFemCheckpoint] ERROR: empty checkpoint (Nglobal = 0)" << endl; break;
    case kErrGidRange:         cerr << "[FreeFemCheckpoint] ERROR: local gid outside canonical range [0, Nglobal)" << endl; break;
    case kErrBlockTooLarge:    cerr << "[FreeFemCheckpoint] ERROR: local canonical block exceeds INT_MAX" << endl; break;
    case kErrTruncated:        cerr << "[FreeFemCheckpoint] ERROR: truncated checkpoint payload" << endl; break;
    case kErrTrailingBytes:    cerr << "[FreeFemCheckpoint] ERROR: unexpected trailing bytes in checkpoint" << endl; break;
    case kErrRead:             cerr << "[FreeFemCheckpoint] ERROR: MPI-IO read failed" << endl; break;
    case kErrWrite:            cerr << "[FreeFemCheckpoint] ERROR: MPI-IO write failed" << endl; break;
    case kErrInvalidGid:       cerr << "[FreeFemCheckpoint] ERROR: invalid (negative) global DOF ids" << endl; break;
    case kErrCanonicalization: cerr << "[FreeFemCheckpoint] ERROR: canonicalization failed (missing DOFs or conflicting duplicates)" << endl; break;
    case kErrOverflow:         cerr << "[FreeFemCheckpoint] ERROR: MPI_Alltoallv int count/displacement overflow" << endl; break;
    case kErrNonFinite:        cerr << "[FreeFemCheckpoint] ERROR: NaN/Inf value in checkpoint field" << endl; break;
    case kErrComm:             cerr << "[FreeFemCheckpoint] ERROR: MPI communication failed" << endl; break;
    default:                   cerr << "[FreeFemCheckpoint] ERROR: unknown error " << code << endl; break;
    }
}

// =====================================================================
// Block ownership: contiguous canonical I/O blocks over the ranks.
// =====================================================================
static int ownerOfGlobalDof(long long g, long long nGlobal, int size)
{
    // Block distribution:
    // first rem ranks own q+1 entries, remaining ranks own q entries.
    const long long q   = nGlobal / size;
    const long long rem = nGlobal % size;
    const long long cut = (q + 1) * rem;

    if (g < cut)
        return static_cast<int>(g / (q + 1));

    // q can only be zero when rem == nGlobal, hence every valid g is
    // handled by the branch above.
    return static_cast<int>(rem + (g - cut) / q);
}

static void computeBlockOwnership(long long nGlobal, int rank, int size,
                                  long long& begin, long long& n)
{
    const long long q   = nGlobal / size;
    const long long rem = nGlobal % size;

    if (rank < rem) {
        n     = q + 1;
        begin = rank * (q + 1);
    } else {
        n     = q;
        begin = rem * (q + 1) + (rank - rem) * q;
    }
}

// =====================================================================
// Count/displacement validation (MPI int counts, 64-bit accumulation)
// =====================================================================
bool buildDisplacementsChecked(const vector<int>& counts,
                               vector<int>& disps, long long& total)
{
    const int n = static_cast<int>(counts.size());
    disps.assign(n, 0);
    long long running = 0;
    for (int r = 0; r < n; ++r) {
        disps[r] = static_cast<int>(running);
        running += counts[r];
        if (running > INT_MAX)
            return false;
    }
    total = running;
    return true;
}

// =====================================================================
// Duplicate consistency policy: finite values with relative tolerance;
// non-finite values are rejected before this check.
// =====================================================================
bool valuesConsistent(double a, double b)
{
    if (!std::isfinite(a) || !std::isfinite(b))
        return false;

    const double tol =
        kDuplicateTolerance * (1.0 + std::max(std::abs(a), std::abs(b)));
    return std::abs(a - b) <= tol;
}

bool allFinite(const double* v, long long n)
{
    for (long long i = 0; i < n; ++i)
        if (!std::isfinite(v[i]))
            return false;
    return true;
}

// =====================================================================
// Canonical writer
// =====================================================================
static long checkpointWriteImpl(string* const& filename,
                                KN<long>* const& gid,
                                KN<double>* const& values)
{
    MPI_Comm comm = checkpointComm();
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    // ---- argument validation (collectively agreed) ----
    int localCode = 0;
    if (!filename || !gid || !values)
        localCode = kErrInvalidArgument;
    else if (gid->N() != values->N())
        localCode = kErrSizeMismatch;
    const long long localN = values
        ? static_cast<long long>(values->N()) : 0;
    if (localCode == 0 && localN > INT_MAX)
        localCode = kErrLocalTooLarge;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    // ---- infer Nglobal = max(gid) + 1 ----
    long long localMax = -1;
    long long localMin = LLONG_MAX;
    for (long long i = 0; i < localN; ++i) {
        const long long g = static_cast<long long>((*gid)[i]);
        localMax = std::max(localMax, g);
        localMin = std::min(localMin, g);
    }
    long long globalMax = -1;
    long long globalMin = LLONG_MAX;
    MPI_Allreduce(&localMax, &globalMax, 1, MPI_LONG_LONG, MPI_MAX, comm);
    MPI_Allreduce(&localMin, &globalMin, 1, MPI_LONG_LONG, MPI_MIN, comm);

    if (globalMax < 0 || globalMin < 0 || globalMax == LLONG_MAX)
        localCode = kErrInvalidGid;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }
    const long long nGlobal = globalMax + 1;

    // ---- local gid range ----
    for (long long i = 0; i < localN; ++i) {
        const long long g = static_cast<long long>((*gid)[i]);
        if (g < 0 || g >= nGlobal) { localCode = kErrGidRange; break; }
    }
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    // ---- non-finite policy: reject NaN/Inf checkpoint values ----
    if (localN && !allFinite(&(*values)[0], localN))
        localCode = kErrNonFinite;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    // ------------------------------------------------------------------
    // 1. Route every (global ID, value) pair to the rank that owns the
    //    corresponding contiguous canonical I/O block.
    // ------------------------------------------------------------------
    vector<int> sendCount(size, 0);
    for (long long i = 0; i < localN; ++i) {
        const long long g = static_cast<long long>((*gid)[i]);
        ++sendCount[ownerOfGlobalDof(g, nGlobal, size)];
    }

    vector<int> sendDisp(size, 0);
    long long totalSend = 0;
    if (!buildDisplacementsChecked(sendCount, sendDisp, totalSend))
        localCode = kErrOverflow;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    vector<int> recvCount(size, 0);
    int ierr = MPI_Alltoall(sendCount.data(), 1, MPI_INT,
                            recvCount.data(), 1, MPI_INT, comm);
    if (ierr != MPI_SUCCESS)
        localCode = kErrComm;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    vector<int> recvDisp(size, 0);
    long long totalRecv = 0;
    if (!buildDisplacementsChecked(recvCount, recvDisp, totalRecv))
        localCode = kErrOverflow;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    vector<long long> sendGid(static_cast<size_t>(totalSend));
    vector<double>    sendVal(static_cast<size_t>(totalSend));
    vector<int> cursor = sendDisp;

    for (long long i = 0; i < localN; ++i) {
        const long long g = static_cast<long long>((*gid)[i]);
        const int dest = ownerOfGlobalDof(g, nGlobal, size);
        const int p = cursor[dest]++;
        sendGid[p] = g;
        sendVal[p] = (*values)[i];
    }

    vector<long long> recvGid(static_cast<size_t>(totalRecv));
    vector<double>    recvVal(static_cast<size_t>(totalRecv));

    ierr = MPI_Alltoallv(sendGid.data(), sendCount.data(), sendDisp.data(),
                         MPI_LONG_LONG,
                         recvGid.data(), recvCount.data(), recvDisp.data(),
                         MPI_LONG_LONG, comm);
    if (ierr != MPI_SUCCESS)
        localCode = kErrComm;

    if (localCode == 0) {
        ierr = MPI_Alltoallv(sendVal.data(), sendCount.data(), sendDisp.data(),
                             MPI_DOUBLE,
                             recvVal.data(), recvCount.data(), recvDisp.data(),
                             MPI_DOUBLE, comm);
        if (ierr != MPI_SUCCESS)
            localCode = kErrComm;
    }
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    // ------------------------------------------------------------------
    // 2. Deduplicate on the canonical owner.
    // ------------------------------------------------------------------
    long long ioBegin = 0, ioN = 0;
    computeBlockOwnership(nGlobal, rank, size, ioBegin, ioN);

    if (ioN > INT_MAX)
        localCode = kErrBlockTooLarge;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    vector<double> ioValue(static_cast<size_t>(ioN), 0.0);
    vector<unsigned char> seen(static_cast<size_t>(ioN), 0);

    long long localDuplicates = 0;
    long long localMissing = 0;
    long long localConflicts = 0;

    for (long long i = 0; i < totalRecv; ++i) {
        const long long g = recvGid[static_cast<size_t>(i)];
        if (g < ioBegin || g >= ioBegin + ioN) {
            ++localConflicts; // routing invariant violated
            continue;
        }

        const size_t j = static_cast<size_t>(g - ioBegin);
        const double v = recvVal[static_cast<size_t>(i)];

        if (!seen[j]) {
            ioValue[j] = v;
            seen[j] = 1;
        } else {
            ++localDuplicates;
            if (!valuesConsistent(ioValue[j], v))
                ++localConflicts;
        }
    }

    for (long long i = 0; i < ioN; ++i)
        if (!seen[static_cast<size_t>(i)])
            ++localMissing;

    long long globalDuplicates = 0;
    long long globalMissing = 0;
    long long globalConflicts = 0;
    long long globalInput = 0;
    MPI_Allreduce(&localDuplicates, &globalDuplicates,
                  1, MPI_LONG_LONG, MPI_SUM, comm);
    MPI_Allreduce(&localMissing, &globalMissing,
                  1, MPI_LONG_LONG, MPI_SUM, comm);
    MPI_Allreduce(&localConflicts, &globalConflicts,
                  1, MPI_LONG_LONG, MPI_SUM, comm);
    MPI_Allreduce(&localN, &globalInput,
                  1, MPI_LONG_LONG, MPI_SUM, comm);

    if (globalMissing || globalConflicts) {
        localCode = kErrCanonicalization;
        const long code = checkpointFail(localCode, comm);
        if (rank == 0) {
            cerr << "[FreeFemCheckpoint] write: ERROR: canonicalization failed: "
                 << globalMissing << " missing DOFs, "
                 << globalConflicts << " conflicting duplicate values."
                 << endl;
        }
        return code;
    }

    // ------------------------------------------------------------------
    // 3. Write canonical global vector collectively (single 64-byte
    //    header buffer, one header write, collective payload write).
    // ------------------------------------------------------------------
    MPI_File fh;
    ierr = MPI_File_open(comm, const_cast<char*>(filename->c_str()),
                         MPI_MODE_CREATE | MPI_MODE_WRONLY,
                         MPI_INFO_NULL, &fh);
    {
        const long code = checkpointFail(ierr == MPI_SUCCESS ? 0 : kErrOpen, comm);
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code; // ranks with a successful open have no valid collective
                         // close path here; MPI_File_open failures are expected
                         // to be uniform (documented limitation)
        }
    }

    ierr = MPI_File_set_size(fh, 0); // overwrite/truncate semantics
    {
        const long code = checkpointFail(ierr == MPI_SUCCESS ? 0 : kErrWrite, comm);
        if (code != 0) {
            if (rank == 0) printFailure(code);
            MPI_File_close(&fh);
            return code;
        }
    }

    array<unsigned char, kHeaderSize> hdr{};
    const uint64_t version = kFormatVersion;
    const uint64_t nGlobal64 = static_cast<uint64_t>(nGlobal);
    const uint64_t width64 = static_cast<uint64_t>(sizeof(double));
    const uint64_t size64 = static_cast<uint64_t>(size);
    const uint64_t input64 = static_cast<uint64_t>(globalInput);
    const uint64_t dup64 = static_cast<uint64_t>(globalDuplicates);
    const uint64_t reserved64 = 0;
    memcpy(hdr.data() + 0,  kMagic,    8);
    memcpy(hdr.data() + 8,  &version,   8);
    memcpy(hdr.data() + 16, &nGlobal64, 8);
    memcpy(hdr.data() + 24, &width64,   8);
    memcpy(hdr.data() + 32, &size64,    8);
    memcpy(hdr.data() + 40, &input64,   8);
    memcpy(hdr.data() + 48, &dup64,     8);
    memcpy(hdr.data() + 56, &reserved64, 8);

    MPI_Status st;
    if (rank == 0)
        ierr = MPI_File_write_at(fh, 0, hdr.data(),
                                 static_cast<int>(kHeaderSize), MPI_BYTE, &st);
    int localErr = (rank != 0 || ierr == MPI_SUCCESS) ? 0 : 1;
    if (collectiveAnyError(localErr, comm)) {
        const long code = checkpointFail(kErrWrite, comm);
        if (rank == 0) printFailure(code);
        MPI_File_close(&fh);
        return code;
    }

    MPI_Barrier(comm);

    const MPI_Offset payloadOffset =
        static_cast<MPI_Offset>(kHeaderSize) +
        static_cast<MPI_Offset>(ioBegin) *
        static_cast<MPI_Offset>(sizeof(double));

    double* ptr = ioN ? ioValue.data() : nullptr;
    ierr = MPI_File_write_at_all(fh, payloadOffset, ptr,
                                 static_cast<int>(ioN), MPI_DOUBLE, &st);
    localErr = (ierr == MPI_SUCCESS) ? 0 : 1;
    const long code = checkpointFail(localErr ? kErrWrite : 0, comm);
    ierr = MPI_File_close(&fh);
    if (localErr && rank == 0)
        printFailure(kErrWrite);
    if (code != 0)
        return code;

    if (rank == 0)
        cout << "[FreeFemCheckpoint] write: input " << globalInput
             << " overlapping values -> " << nGlobal
             << " unique global DOFs (removed " << globalDuplicates
             << " duplicates), file = " << *filename << endl;

    return static_cast<long>(nGlobal);
}

// =====================================================================
// Canonical reader
// =====================================================================
static long checkpointReadImpl(string* const& filename,
                               KN<long>* const& gid,
                               KN<double>* const& values)
{
    MPI_Comm comm = checkpointComm();
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    int localCode = 0;
    if (!filename || !gid || !values)
        localCode = kErrInvalidArgument;
    else if (gid->N() != values->N())
        localCode = kErrSizeMismatch;
    const long long localN = values
        ? static_cast<long long>(values->N()) : 0;
    if (localCode == 0 && localN > INT_MAX)
        localCode = kErrLocalTooLarge;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    MPI_File fh;
    int ierr = MPI_File_open(comm, const_cast<char*>(filename->c_str()),
                             MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
    {
        const long code = checkpointFail(ierr == MPI_SUCCESS ? 0 : kErrOpen, comm);
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code; // see writer comment about uniform open failures
        }
    }

    // ------------------------------------------------------------------
    // 1. Validate the file size BEFORE decoding a possibly short header,
    //    then read exactly 64 header bytes collectively.
    // ------------------------------------------------------------------
    MPI_Offset fileSize = 0;
    ierr = MPI_File_get_size(fh, &fileSize);
    localCode = (ierr == MPI_SUCCESS) ? 0 : kErrHeader;
    if (localCode == 0 && fileSize < kHeaderSize)
        localCode = kErrHeader; // truncated header
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            MPI_File_close(&fh);
            return code;
        }
    }

    array<unsigned char, kHeaderSize> hdr{};
    MPI_Status st;
    ierr = MPI_File_read_at_all(fh, 0, hdr.data(),
                                static_cast<int>(kHeaderSize), MPI_BYTE, &st);
    int count = 0;
    if (ierr == MPI_SUCCESS)
        MPI_Get_count(&st, MPI_BYTE, &count);
    localCode = (ierr == MPI_SUCCESS && count == kHeaderSize) ? 0 : kErrHeader;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            MPI_File_close(&fh);
            return code;
        }
    }

    // Native (host) uint64 fields, written as explicit byte-offset fields.
    uint64_t version = 0, nGlobal = 0, width = 0, reserved = 0;
    memcpy(&version,  hdr.data() + 8,  8);
    memcpy(&nGlobal,  hdr.data() + 16, 8);
    memcpy(&width,    hdr.data() + 24, 8);
    memcpy(&reserved, hdr.data() + 56, 8);
    (void)reserved;

    if (memcmp(hdr.data(), kMagic, 8) != 0)
        localCode = kErrMagic;
    else if (version != kFormatVersion)
        localCode = kErrVersion;
    else if (width != sizeof(double))
        localCode = kErrScalarWidth;
    else if (nGlobal == 0)
        localCode = kErrEmpty;
    else if (nGlobal > static_cast<uint64_t>(LLONG_MAX))
        localCode = kErrHeader;
    else if (nGlobal > (UINT64_MAX - static_cast<uint64_t>(kHeaderSize)) / width)
        localCode = kErrHeader; // expected-size arithmetic overflow
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            MPI_File_close(&fh);
            return code;
        }
    }

    // ------------------------------------------------------------------
    // 2. Strict exact-size policy: fileSize == 64 + Nglobal * width.
    // ------------------------------------------------------------------
    const uint64_t expected =
        static_cast<uint64_t>(kHeaderSize) + nGlobal * width;
    if (static_cast<uint64_t>(fileSize) < expected)
        localCode = kErrTruncated;
    else if (static_cast<uint64_t>(fileSize) > expected)
        localCode = kErrTrailingBytes;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            MPI_File_close(&fh);
            return code;
        }
    }

    const long long nGlobalLL = static_cast<long long>(nGlobal);

    // ------------------------------------------------------------------
    // 3. Validate local gids against the checkpoint's canonical range.
    // ------------------------------------------------------------------
    for (long long i = 0; i < localN; ++i) {
        const long long g = static_cast<long long>((*gid)[i]);
        if (g < 0 || g >= nGlobalLL) { localCode = kErrGidRange; break; }
    }
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            MPI_File_close(&fh);
            return code;
        }
    }

    // ------------------------------------------------------------------
    // 4. Block-wise collective read of the canonical payload.
    // ------------------------------------------------------------------
    long long ioBegin = 0, ioN = 0;
    computeBlockOwnership(nGlobalLL, rank, size, ioBegin, ioN);

    if (ioN > INT_MAX)
        localCode = kErrBlockTooLarge;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            MPI_File_close(&fh);
            return code;
        }
    }

    vector<double> ioVal(static_cast<size_t>(ioN), 0.0);
    double* ptr = ioN ? ioVal.data() : nullptr;
    const MPI_Offset payloadOffset =
        static_cast<MPI_Offset>(kHeaderSize) +
        static_cast<MPI_Offset>(ioBegin) *
        static_cast<MPI_Offset>(sizeof(double));

    ierr = MPI_File_read_at_all(fh, payloadOffset, ptr,
                                static_cast<int>(ioN), MPI_DOUBLE, &st);
    localCode = (ierr == MPI_SUCCESS) ? 0 : kErrRead;

    int closeErr = MPI_File_close(&fh);
    if (localCode == 0 && closeErr != MPI_SUCCESS)
        localCode = kErrRead;

    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    if (!allFinite(ioVal.data(), ioN))
        localCode = kErrNonFinite;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    // ------------------------------------------------------------------
    // 5. Request redistribution: every local DOF asks its current owner
    //    for payload[ gidLocal[i] ].  Overlapping copies may issue
    //    duplicate requests; each is answered independently.
    // ------------------------------------------------------------------
    vector<int> sendCount(size, 0);
    for (long long i = 0; i < localN; ++i) {
        const long long g = static_cast<long long>((*gid)[i]);
        ++sendCount[ownerOfGlobalDof(g, nGlobalLL, size)];
    }

    vector<int> sendDisp(size, 0);
    long long totalSend = 0;
    if (!buildDisplacementsChecked(sendCount, sendDisp, totalSend))
        localCode = kErrOverflow;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    vector<int> recvCount(size, 0);
    ierr = MPI_Alltoall(sendCount.data(), 1, MPI_INT,
                        recvCount.data(), 1, MPI_INT, comm);
    if (ierr != MPI_SUCCESS)
        localCode = kErrComm;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    vector<int> recvDisp(size, 0);
    long long totalRecv = 0;
    if (!buildDisplacementsChecked(recvCount, recvDisp, totalRecv))
        localCode = kErrOverflow;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    // Requests are packed owner-by-owner, and within each owner chunk in
    // ascending local-DOF order, so answers can be unpacked without extra
    // index traffic.
    vector<long long> sendGid(static_cast<size_t>(totalSend));
    vector<int> cursor = sendDisp;
    for (long long i = 0; i < localN; ++i) {
        const long long g = static_cast<long long>((*gid)[i]);
        const int dest = ownerOfGlobalDof(g, nGlobalLL, size);
        sendGid[cursor[dest]++] = g;
    }

    vector<long long> recvGid(static_cast<size_t>(totalRecv));
    ierr = MPI_Alltoallv(sendGid.data(), sendCount.data(), sendDisp.data(),
                         MPI_LONG_LONG,
                         recvGid.data(), recvCount.data(), recvDisp.data(),
                         MPI_LONG_LONG, comm);
    if (ierr != MPI_SUCCESS)
        localCode = kErrComm;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    // ------------------------------------------------------------------
    // 6. Owners answer from their canonical block, then the answers are
    //    returned along the transposed communication pattern.
    // ------------------------------------------------------------------
    vector<double> answer(static_cast<size_t>(totalRecv));
    for (long long j = 0; j < totalRecv; ++j) {
        const long long g = recvGid[static_cast<size_t>(j)];
        answer[static_cast<size_t>(j)] =
            ioVal[static_cast<size_t>(g - ioBegin)];
    }

    vector<int> retSendCount = recvCount; // owner -> requester counts
    vector<int> retRecvCount = sendCount; // requester <- owner counts
    vector<int> retSendDisp(size, 0), retRecvDisp(size, 0);
    long long totalRetSend = 0, totalRetRecv = 0;
    if (!buildDisplacementsChecked(retSendCount, retSendDisp, totalRetSend) ||
        !buildDisplacementsChecked(retRecvCount, retRecvDisp, totalRetRecv))
        localCode = kErrOverflow;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    vector<double> retVals(static_cast<size_t>(totalSend));
    ierr = MPI_Alltoallv(answer.data(), retSendCount.data(), retSendDisp.data(),
                         MPI_DOUBLE,
                         retVals.data(), retRecvCount.data(), retRecvDisp.data(),
                         MPI_DOUBLE, comm);
    if (ierr != MPI_SUCCESS)
        localCode = kErrComm;
    {
        const long code = checkpointFail(localCode, comm); // unconditional: all ranks reduce
        if (code != 0) {
            if (rank == 0) printFailure(code);
            return code;
        }
    }

    // ------------------------------------------------------------------
    // 7. Unpack: answers arrive owner-by-owner (ascending owner rank), and
    //    within an owner chunk in the same ascending local-DOF order used
    //    for the requests.
    // ------------------------------------------------------------------
    vector<long long> base(size, 0);
    for (int d = 1; d < size; ++d)
        base[d] = base[d - 1] + sendCount[d - 1];
    vector<long long> used(size, 0);

    for (long long i = 0; i < localN; ++i) {
        const long long g = static_cast<long long>((*gid)[i]);
        const int dest = ownerOfGlobalDof(g, nGlobalLL, size);
        const long long p = base[dest] + used[dest]++;
        (*values)[i] = retVals[static_cast<size_t>(p)];
    }

    if (rank == 0)
        cout << "[FreeFemCheckpoint] read: " << nGlobalLL
             << " canonical DOFs from " << *filename << " with " << size
             << " MPI ranks." << endl;

    return static_cast<long>(nGlobalLL);
}

} // namespace

static void Load_Init()
{
    if (verbosity > 0 && mpirank == 0)
        cout << "load: FreeFemCheckpoint 0.3.0 (FFIOG001, N-to-M MPI-IO)"
             << endl;

    // Public API
    Global.Add("ffcpWrite", "(",
               new OneOperator3_<long, string*, KN<long>*, KN<double>*>(
                   checkpointWriteImpl));
    Global.Add("ffcpRead", "(",
               new OneOperator3_<long, string*, KN<long>*, KN<double>*>(
                   checkpointReadImpl));

    // Legacy aliases (same implementation, kept for backward compatibility)
    Global.Add("ffioWriteGlobal", "(",
               new OneOperator3_<long, string*, KN<long>*, KN<double>*>(
                   checkpointWriteImpl));
    Global.Add("ffioReadGlobal", "(",
               new OneOperator3_<long, string*, KN<long>*, KN<double>*>(
                   checkpointReadImpl));
}

LOADFUNC(Load_Init)
