#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef size_t SIZE_T;
typedef unsigned long ULONG;
typedef int NTSTATUS;

#define STATUS_SUCCESS 0
#define STATUS_NO_MEMORY (-1)
#define GPUSION_DEVICE_SIGNATURE 0x47505553U
#define GPUSION_ALLOC_SIGNATURE  0x4750414CU

typedef struct { long long QuadPart; } PHYSICAL_ADDRESS;

typedef struct {
    ULONG Signature;
    SIZE_T SizeBytes;
    void *SystemMemory;
    PHYSICAL_ADDRESS VirtualGpuAddress;
    int IsCpuVisible;
    int IsShared;
} GPUSION_ALLOCATION, *PGPUSION_ALLOCATION;

typedef struct {
    ULONG Signature;
    struct {
        SIZE_T TotalBytes;
        SIZE_T AllocatedBytes;
    } Vram;
    struct {
        long long BytesAllocated;
        long long BytesFreed;
    } Stats;
} GPUSION_DEVICE_CONTEXT;

typedef struct {
    SIZE_T Size;
    void *pDriverData;
    SIZE_T DriverDataSize;
} DXGK_ALLOCATIONINFO;

typedef struct {
    ULONG NumAllocations;
    DXGK_ALLOCATIONINFO *pAllocationInfo;
    struct { int SharedResource; } Flags;
} DXGKARG_CREATEALLOCATION;

static void *pool_alloc(size_t n)
{
    return calloc(1, n);
}

/*
 * Minimal source-faithful model of the allocation/accounting portion of
 * driver/vram/vram_proxy.c::GpusionCreateAllocation at upstream main
 * ebdbd2473456fcff033baa904ac3fe1f2e6e105b.
 *
 * It intentionally preserves the current behavior relevant to this report:
 * allocations increment Vram.AllocatedBytes but never compare the requested
 * size against Vram.TotalBytes.
 */
static NTSTATUS
model_create(GPUSION_DEVICE_CONTEXT *Context,
             DXGKARG_CREATEALLOCATION *req)
{
    ULONG i;

    for (i = 0; i < req->NumAllocations; i++) {
        DXGK_ALLOCATIONINFO *info = &req->pAllocationInfo[i];
        SIZE_T SizeBytes = info->Size;
        PGPUSION_ALLOCATION Alloc = pool_alloc(sizeof(*Alloc));

        if (!Alloc)
            return STATUS_NO_MEMORY;

        Alloc->Signature = GPUSION_ALLOC_SIGNATURE;
        Alloc->SizeBytes = SizeBytes;
        Alloc->SystemMemory = pool_alloc(SizeBytes);
        if (!Alloc->SystemMemory) {
            free(Alloc);
            return STATUS_NO_MEMORY;
        }

        Alloc->VirtualGpuAddress.QuadPart =
            (long long)(uintptr_t)Alloc->SystemMemory;
        Alloc->IsCpuVisible = 1;
        Alloc->IsShared = req->Flags.SharedResource != 0;

        info->pDriverData = Alloc;
        info->DriverDataSize = sizeof(*Alloc);

        /* Matches the current source accounting path. */
        Context->Vram.AllocatedBytes += SizeBytes;
        Context->Stats.BytesAllocated += (long long)SizeBytes;
    }

    return STATUS_SUCCESS;
}

static void
cleanup(DXGKARG_CREATEALLOCATION *req)
{
    ULONG i;

    for (i = 0; i < req->NumAllocations; i++) {
        PGPUSION_ALLOCATION a = req->pAllocationInfo[i].pDriverData;
        if (a) {
            free(a->SystemMemory);
            free(a);
            req->pAllocationInfo[i].pDriverData = NULL;
        }
    }
}

int
main(void)
{
    GPUSION_DEVICE_CONTEXT ctx = {0};
    DXGK_ALLOCATIONINFO infos[2] = {
        {.Size = 800},
        {.Size = 400},
    };
    DXGKARG_CREATEALLOCATION req = {
        .NumAllocations = 2,
        .pAllocationInfo = infos,
    };
    NTSTATUS st;
    int bug;

    ctx.Signature = GPUSION_DEVICE_SIGNATURE;

    /* Scaled-down stand-in for the driver's advertised 8 GiB VRAM budget. */
    ctx.Vram.TotalBytes = 1024;

    st = model_create(&ctx, &req);
    printf("status=%d total=%zu allocated=%zu over_by=%zu\n",
           st,
           ctx.Vram.TotalBytes,
           ctx.Vram.AllocatedBytes,
           ctx.Vram.AllocatedBytes > ctx.Vram.TotalBytes
               ? ctx.Vram.AllocatedBytes - ctx.Vram.TotalBytes
               : 0);

    bug = (st == STATUS_SUCCESS &&
           ctx.Vram.AllocatedBytes > ctx.Vram.TotalBytes);
    printf("budget_exceeded=%s\n", bug ? "YES" : "NO");

    cleanup(&req);
    return bug ? 1 : 0;
}
