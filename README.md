## PMC Bruteforcer

PMC Bruteforcer is a UEFI application designed to discover hidden CPU instructions and log them to a save file on a boot device.

## How It Works

The project utilizes hardware performance counters (PMCs) to detect hidden instructions. The primary performance counters used are:
* `UOPS_ISSUED.ANY`
* `UOPS_RETIRED.ALL`
* `UOPS_RETIRED.RETIRE_SLOTS`

### Core Logic
It assumes that hidden instructions perform internal checks in their microcode before raising an invalid opcode (`#UD`) exception. This architectural behavior inevitably leads to a larger number of issued and retired micro-ops after their execution when compared to the execution of an instruction designed to always raise `#UD` (e.g., `ud2`).

To obtain consistent counter values, this UEFI utility:
1. Enables the counters strictly in user space.
2. Flushes the user code page immediately after writing the instruction bytes to execute.
3. Executes the instruction at least two times until the performance counter values from the last run match the results from the previous one (only performed if the instruction is a candidate to be saved into a file, and this decision is based solely on the performance counters' values).

### Filtering Criteria
An instruction is saved if it meets one of the following criteria:
1. **If it raises `#UD`:** The PMC values do not match those of `ud2`, **and** Intel XED fails to recognize the instruction, or the instruction lengths from Intel XED and the CPU decoder do not match. This is done to avoid saving documented but disabled instructions (e.g., Intel VMX instructions).
2. **It raises a kernel-mode exception:** Unexpected behavior when executing code in user space.
3. **It raises a machine-check (`#MC`) exception:** Architectural anomaly for user-space execution.

### Search Algorithm
To cover the entire instruction space efficiently, a modified version of Christopher Domas' tunneling algorithm is used.

Specifically, it allows filtering the instructions by the count and the contents of their prefixes. This significantly speeds up the brute-force process, as prefixes like EVEX can drastically increase overall execution time.
Also, when one of the prefix bytes is incremented, the increment pointer is forced to point to the last byte of the instruction. This ensures that the new instruction prefix combination is not skipped by accident when the instruction length stays the same after incrementing one of the prefix bytes.

## Supported Platforms

Currently, only Intel Skylake and Alder Lake CPUs are supported. However, support for other CPUs can be added by inserting new performance counter indexes. See `src/instruction_execution_loop.c` and `src/disasm.c` to get an idea of how it is implemented.

AMD CPU support is planned (requires investigating what specific counters they offer).

## Building

### Prerequisites
* **Compiler:** `clang` (with `lld` linker)
* **Build System:** `make`
* **Headers:** `gnu-efi`
* **Tools:** Git binary

### Building the UEFI Application
1. Clone the repository and initialize submodules:
```bash
git clone https://github.com/unknown-1-0/x86_pmc_instruction_bruteforce
cd x86_pmc_instruction_bruteforce
git submodule update --init --recursive
```

2. Build the Intel XED dependency:
```bash
./build_xed.sh
```

3. Compile the target binary (specify your CPU mode and microarchitecture):
```bash
make CPU_MODE=<16/32/64> TARGET_UARCH=<SKYLAKE/ALDER_LAKE> BOOTX64.EFI
```

## Running the Application

### Bare Metal Execution
Once compiled, the resulting `BOOTX64.EFI` file should be placed into the `/EFI/BOOT/BOOTX64.EFI` path on a FAT32-formatted USB drive. 

Alternatively, it can be executed manually from the UEFI Shell. This is the preferred method if you plan to run the bruteforcer across different CPU modes.

*Note: **Secure Boot must be disabled** in your firmware settings before running the binary. Otherwise, the platform will block the execution of this unsigned EFI executable.*

### Emulation and Testing
If `BOOTX64.EFI` is not explicitly specified as the build target in `make`, an `app.img` disk image will also be generated. This image can be used to quickly verify that the application functions correctly without rebooting your host machine by using QEMU+KVM:

```bash
qemu-system-x86_64 -cpu host -enable-kvm -drive file=./app.img,format=raw -bios /path/to/OVMF.fd
```

**Warning:** Emulation should not be used for actual brute-forcing sessions because:
1. The default image size is too small to store a reasonable volume of logged data.
2. KVM's pre-emption introduces non-determinism into the performance counter metrics, yielding inaccurate values.

## Future Plans
* Integrate `IDQ.*_UOPS` counters (currently in testing)
* Implement AMD CPU support
* Add proper handling for CPUs that lack `XSAVE` support
* Provide source code for a results decoder application
* Add the ability to repeatedly execute specific instructions under different configurations (`CR4`, MSR values)
* Verify the existence of CET-related instructions that are described in an Intel patent named "Control transfer termination instructions of an instruction set architecture (ISA)" (US11023232B2)
