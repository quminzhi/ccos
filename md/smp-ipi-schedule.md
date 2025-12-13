# SMP Boot & IPI Scheduling Snapshot

This note captures the current design for multi-core bring-up and how we use IPIs to drive scheduling.

## Boot model
- **Boot hart is chosen by OpenSBI**, not assumed to be 0. It jumps into our `_start` → `kernel_main` in S-mode.
- Other harts are started later via `sbi_hart_start(h, secondary_entry, dtb_pa)`, entering `secondary_entry` without re-clearing BSS.
- Each hart sets up its own kernel stack and `sscratch` pointing to `cpu_t`. `tp` always holds `cpu*`.
- Per-hart idle thread: `tid == hartid`, lives in `g_threads`, used when no runnable user thread exists.
- All harts enable `SSIP/SEIP/STIP` so they can respond to IPIs, PLIC, and timer interrupts.

## Timer responsibility
- **Only the boot hart programs the periodic timer** (current design). On each tick it:
  - Advances `g_ticks`.
  - Wakes SLEEPING threads whose `wakeup_tick <= g_ticks`, marking them RUNNABLE.
  - If any thread was woken, calls `sched_notify_runnable()` to poke other harts.
- This keeps timer traffic simple while still letting other harts participate in scheduling.

## Scheduling model
- Round-robin per hart across RUNNABLE threads in `g_threads`.
- If nothing runnable, the hart runs its idle thread.
- Context accounting:
  - `running_hart` set when a thread is scheduled; cleared (and copied to `last_hart`) when it is descheduled.
  - `runs` counts entries into RUNNING; `migrations` increments when `last_hart != current hart`.

## IPI strategy
- We use the SBI v0.2+ IPI extension with **scalar hart masks**:
  - `sbi_send_ipi(1UL, target_hart)` sends SSIP to one hart (bit0 corresponds to `hart_mask_base = target`).
- When do we send IPIs?
  - On the boot hart after waking sleepers in `threads_tick()` if any became RUNNABLE.
  - Anywhere else that makes a thread RUNNABLE (e.g., unblock, create) via `sched_notify_runnable()`.
- Expected effect:
  - Harts in WFI will receive SSIP and enter the scheduler.
  - RUNNING harts may see the pending SSIP and schedule at the next trap boundary.

## Invariants & assumptions
- `MAX_HARTS` sized arrays for `cpu_t`, `g_threads`, and stacks; `THREAD_MAX >= MAX_HARTS + 1` to accommodate all idle threads + user/kernel threads.
- Idle tids are reserved: `tid == hartid`.
- All harts share a single ready list (`g_threads[]`); there is no per-hart run queue.
- PLIC and timer init are per-hart, but device interrupts may be delivered to multiple harts if enabled upstream.

## Known limitations / future options
- Timer is boot-hart-only; moving to per-hart timers would reduce wakeup latency and balance tick load.
- Single global run queue means more lock contention if many harts and high runnable counts; per-hart queues with work stealing could improve scaling.
- IPI mask is currently “one bit per send” for clarity. A batched mask (multiple bits set) could reduce ECALL overhead when waking many harts at once.
- No load balancing heuristics beyond round-robin; threads may stick to last hart unless woken elsewhere.

## Debugging checklist
- If interrupts “vanish” on non-zero boot hart, verify all IPIs use scalar masks and `hart_mask_base` is set to the target hart.
- Ensure SSIP/SEIP/STIP are enabled on every hart after secondary entry.
- Use `monitor` to confirm:
  - `CPU` shows current hart or `---` when not running.
  - `LAST` shows the last hart; `MIG` grows when threads move.
