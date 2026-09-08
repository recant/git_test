#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

typedef int32_t NTSTATUS;
typedef uint32_t ULONG, UINT32;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef size_t SIZE_T;
typedef uint8_t BOOLEAN;
typedef void *PVOID;

#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define GPUSION_DEVICE_SIGNATURE 0x47505553U
#define GPUSION_VRAM_SIZE_BYTES (8192ULL * 1024ULL * 1024ULL)
#define PAGE_SIZE 4096UL
#define RtlZeroMemory(d, n) memset((d), 0, (n))
#define UNREFERENCED_PARAMETER(p) ((void)(p))

typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;

typedef struct {
    LARGE_INTEGER HighestAcceptableAddress;
    ULONG MaxAllocationListSlotId;
    ULONGLONG ApertureSegmentCommitLimit;
    ULONG MaxPointerWidth;
    ULONG MaxPointerHeight;
    struct { ULONG Value; } SchedulingCaps;
    struct { ULONG Value; } MemoryManagementCaps;
} DXGK_DRIVERCAPS;

typedef struct {
    LARGE_INTEGER BaseAddress;
    LARGE_INTEGER CpuTranslatedAddress;
    SIZE_T Size;
    SIZE_T CommitLimit;
    struct {
        UINT32 CpuVisible : 1;
        UINT32 UseBanking : 1;
        UINT32 Aperture : 1;
        UINT32 Reserved : 29;
    } Flags;
} DXGK_SEGMENTDESCRIPTOR;

typedef struct {
    ULONG NbSegment;
    DXGK_SEGMENTDESCRIPTOR *pSegmentDescriptor;
    ULONG PagingBufferSize;
    ULONG PagingBufferPrivateDataSize;
} DXGK_QUERYSEGMENTOUT;

typedef enum {
    DXGKQAITYPE_DRIVERCAPS = 0,
    DXGKQAITYPE_QUERYSEGMENT = 1,
    DXGKQAITYPE_QUERYSEGMENT3 = 2,
    DXGKQAITYPE_QUERYSEGMENT4 = 3,
    DXGKQAITYPE_UMDRIVERPRIVATE = 4,
    DXGKQAITYPE_PHYSICALADAPTERCAPS = 5
} DXGK_QUERYADAPTERINFOTYPE;

typedef struct {
    DXGK_QUERYADAPTERINFOTYPE Type;
    PVOID pInputData;
    UINT32 InputDataSize;
    PVOID pOutputData;
    UINT32 OutputDataSize;
} DXGKARG_QUERYADAPTERINFO;

typedef struct {
    ULONG Signature;
} GPUSION_DEVICE_CONTEXT, *PGPUSION_DEVICE_CONTEXT;

/* Mirrors current driver/wddm/query_adapter_info.c helper logic. */
static NTSTATUS
GpusionQueryDriverCaps(
    PGPUSION_DEVICE_CONTEXT Context,
    DXGK_DRIVERCAPS *DriverCaps
)
{
    UNREFERENCED_PARAMETER(Context);

    RtlZeroMemory(DriverCaps, sizeof(*DriverCaps));
    DriverCaps->HighestAcceptableAddress.QuadPart = (LONGLONG)-1;
    DriverCaps->MaxAllocationListSlotId = 1;
    DriverCaps->ApertureSegmentCommitLimit = 0x100000000ULL;
    DriverCaps->MaxPointerWidth = 0;
    DriverCaps->MaxPointerHeight = 0;
    DriverCaps->SchedulingCaps.Value = 0;
    DriverCaps->MemoryManagementCaps.Value = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS
GpusionQuerySegmentInfo(
    PGPUSION_DEVICE_CONTEXT Context,
    DXGK_QUERYSEGMENTOUT *SegmentInfo
)
{
    UNREFERENCED_PARAMETER(Context);

    if (SegmentInfo->NbSegment == 0) {
        SegmentInfo->NbSegment = 1;
        SegmentInfo->PagingBufferSize = PAGE_SIZE;
        SegmentInfo->PagingBufferPrivateDataSize = 0;
        return STATUS_SUCCESS;
    }

    if (!SegmentInfo->pSegmentDescriptor)
        return STATUS_INVALID_PARAMETER;

    DXGK_SEGMENTDESCRIPTOR *Seg = &SegmentInfo->pSegmentDescriptor[0];
    RtlZeroMemory(Seg, sizeof(*Seg));
    Seg->Size = GPUSION_VRAM_SIZE_BYTES;
    Seg->CommitLimit = GPUSION_VRAM_SIZE_BYTES;
    Seg->Flags.CpuVisible = 1;
    Seg->Flags.Aperture = 1;
    return STATUS_SUCCESS;
}

static NTSTATUS
GpusionQueryAdapterInfo(
    PVOID pMiniportDeviceContext,
    const DXGKARG_QUERYADAPTERINFO *Query
)
{
    PGPUSION_DEVICE_CONTEXT Context =
        (PGPUSION_DEVICE_CONTEXT)pMiniportDeviceContext;

    if (!Context || Context->Signature != GPUSION_DEVICE_SIGNATURE)
        return STATUS_INVALID_PARAMETER;

    if (!Query || !Query->pOutputData)
        return STATUS_INVALID_PARAMETER;

    switch (Query->Type) {
    case DXGKQAITYPE_DRIVERCAPS:
        return GpusionQueryDriverCaps(
            Context,
            (DXGK_DRIVERCAPS *)Query->pOutputData
        );

    case DXGKQAITYPE_QUERYSEGMENT:
    case DXGKQAITYPE_QUERYSEGMENT3:
    case DXGKQAITYPE_QUERYSEGMENT4:
        return GpusionQuerySegmentInfo(
            Context,
            (DXGK_QUERYSEGMENTOUT *)Query->pOutputData
        );

    default:
        return STATUS_NOT_SUPPORTED;
    }
}

static int
canary_changed(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] != 0xA5)
            return 1;
    }
    return 0;
}

int
main(void)
{
    GPUSION_DEVICE_CONTEXT Context = {
        .Signature = GPUSION_DEVICE_SIGNATURE
    };

    struct {
        unsigned char Advertised[4];
        unsigned char Canary[128];
    } Buffer;

    memset(&Buffer, 0xA5, sizeof(Buffer));

    DXGKARG_QUERYADAPTERINFO Query = {0};
    Query.Type = DXGKQAITYPE_DRIVERCAPS;
    Query.pOutputData = Buffer.Advertised;
    Query.OutputDataSize = sizeof(Buffer.Advertised);

    NTSTATUS Status = GpusionQueryAdapterInfo(&Context, &Query);

    printf(
        "status=0x%08x advertised=%u actual_struct=%zu canary_changed=%s\n",
        (unsigned)Status,
        Query.OutputDataSize,
        sizeof(DXGK_DRIVERCAPS),
        canary_changed(Buffer.Canary, sizeof(Buffer.Canary)) ? "YES" : "NO"
    );

    for (size_t i = 0; i < sizeof(Buffer.Canary); i++) {
        if (Buffer.Canary[i] != 0xA5) {
            printf(
                "first overwritten canary byte offset=%zu value=0x%02x\n",
                i,
                Buffer.Canary[i]
            );
            return 0;
        }
    }

    return 1;
}
