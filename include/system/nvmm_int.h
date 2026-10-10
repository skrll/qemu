/*
 * Internal definitions for a target's NVMM support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_NVMM_INT_H
#define QEMU_NVMM_INT_H

#include "system/memory.h"
#include "qapi/qapi-types-common.h"
#include "qemu/accel.h"
#include "qemu/queue.h"
#include "system/nvmm.h"
#include "accel/accel-ops.h"
#include "hw/boards.h"
//#include "hw/i386/topology.h"
#include "io/channel-socket.h"


#if 0
typedef struct NVMMSlot
{
    hwaddr start_addr;
    ram_addr_t memory_size;
    void *ram;
    int slot;
    int flags;
    int old_flags;
    /* Dirty bitmap cache for the slot */
    unsigned long *dirty_bmap;
    unsigned long dirty_bmap_size;
    /* Cache of the address space ID */
    int as_id;
    /* Cache of the offset in ram address space */
    ram_addr_t ram_start_offset;
    int guest_memfd;
    hwaddr guest_memfd_offset;
} NVMMSlot;

typedef struct NVMMMemoryUpdate {
    QSIMPLEQ_ENTRY(NVMMMemoryUpdate) next;
    MemoryRegionSection section;
} NVMMMemoryUpdate;

typedef struct NVMMMemoryListener {
    MemoryListener listener;
    NVMMSlot *slots;
    unsigned int nr_slots_used;
    unsigned int nr_slots_allocated;
    int as_id;
    QSIMPLEQ_HEAD(, NVMMMemoryUpdate) transaction_add;
    QSIMPLEQ_HEAD(, NVMMMemoryUpdate) transaction_del;
} NVMMMemoryListener;

#define NVMM_MSI_HASHTAB_SIZE    256

typedef struct NVMMHostTopoInfo {
    /* Number of package on the Host */
    unsigned int maxpkgs;
    /* Number of cpus on the Host */
    unsigned int maxcpus;
    /* Number of cpus on each different package */
    unsigned int *pkg_cpu_count;
    /* Each package can have different maxticks */
    unsigned int *maxticks;
} NVMMHostTopoInfo;

struct NVMMMsrEnergy {
    pid_t pid;
    bool enable;
    char *socket_path;
    QIOChannelSocket *sioc;
    QemuThread msr_thr;
    unsigned int guest_vcpus;
    unsigned int guest_vsockets;
    X86CPUTopoInfo guest_topo_info;
    NVMMHostTopoInfo host_topo;
    const CPUArchIdList *guest_cpu_list;
    uint64_t *msr_value;
    uint64_t msr_unit;
    uint64_t msr_limit;
    uint64_t msr_info;
};

enum NVMMDirtyRingReaperState {
    NVMM_DIRTY_RING_REAPER_NONE = 0,
    /* The reaper is sleeping */
    NVMM_DIRTY_RING_REAPER_WAIT,
    /* The reaper is reaping for dirty pages */
    NVMM_DIRTY_RING_REAPER_REAPING,
};

/*
 * NVMM reaper instance, responsible for collecting the NVMM dirty bits
 * via the dirty ring.
 */
struct NVMMDirtyRingReaper {
    /* The reaper thread */
    QemuThread reaper_thr;
    volatile uint64_t reaper_iteration; /* iteration number of reaper thr */
    volatile enum NVMMDirtyRingReaperState reaper_state; /* reap thr state */
};

#endif



struct NVMMState
{
    AccelState parent_obj;



#if 0
    /* Max number of NVMM slots supported */
    int nr_slots_max;
    int fd;
    int vmfd;
    int coalesced_mmio;
    int coalesced_pio;
    struct kvm_coalesced_mmio_ring *coalesced_mmio_ring;
    bool coalesced_flush_in_progress;
    int vcpu_events;
#ifdef TARGET_NVMM_HAVE_GUEST_DEBUG
    QTAILQ_HEAD(, kvm_sw_breakpoint) kvm_sw_breakpoints;
#endif
    int max_nested_state_len;
    int kvm_shadow_mem;
    bool kernel_irqchip_allowed;
    bool kernel_irqchip_required;
    OnOffAuto kernel_irqchip_split;
    bool sync_mmu;
    bool guest_state_protected;
    uint64_t manual_dirty_log_protect;
    /*
     * Older POSIX says that ioctl numbers are signed int, but in
     * practice they are not. (Newer POSIX doesn't specify ioctl
     * at all.) Linux, glibc and *BSD all treat ioctl numbers as
     * unsigned, and real-world ioctl values like NVMM_GET_XSAVE have
     * bit 31 set, which means that passing them via an 'int' will
     * result in sign-extension when they get converted back to the
     * 'unsigned long' which the ioctl() prototype uses. Luckily Linux
     * always treats the argument as an unsigned 32-bit int, so any
     * possible sign-extension is deliberately ignored, but for
     * consistency we keep to the same type that glibc is using.
     */
    unsigned long irq_set_ioctl;
    unsigned int sigmask_len;
    GHashTable *gsimap;
#ifdef NVMM_CAP_IRQ_ROUTING
    struct kvm_irq_routing *irq_routes;
    int nr_allocated_irq_routes;
    unsigned long *used_gsi_bitmap;
    unsigned int gsi_count;
#endif
    NVMMMemoryListener memory_listener;
    QLIST_HEAD(, NVMMParkedVcpu) kvm_parked_vcpus;

    /* For "info mtree -f" to tell if an MR is registered in NVMM */
    int nr_as;
    struct NVMMAs {
        NVMMMemoryListener *ml;
        AddressSpace *as;
    } *as;
    uint64_t kvm_dirty_ring_bytes;  /* Size of the per-vcpu dirty ring */
    uint32_t kvm_dirty_ring_size;   /* Number of dirty GFNs per ring */
    bool kvm_dirty_ring_with_bitmap;
    uint64_t kvm_eager_split_size;  /* Eager Page Splitting chunk size */
    struct NVMMDirtyRingReaper reaper;
    struct NVMMMsrEnergy msr_energy;
    NotifyVmexitOption notify_vmexit;
    uint32_t notify_window;
    uint32_t xen_version;
    uint32_t xen_caps;
    uint16_t xen_gnttab_max_frames;
    uint16_t xen_evtchn_max_pirq;
    char *device;
#endif



};


#if 0
void kvm_memory_listener_register(NVMMState *s, NVMMMemoryListener *kml,
                                  AddressSpace *as, int as_id, const char *name);

void kvm_set_max_memslot_size(hwaddr max_slot_size);

/**
 * kvm_hwpoison_page_add:
 *
 * Parameters:
 *  @ram_addr: the address in the RAM for the poisoned page
 *
 * Add a poisoned page to the list
 *
 * Return: None.
 */
void kvm_hwpoison_page_add(ram_addr_t ram_addr);
#endif




#endif
