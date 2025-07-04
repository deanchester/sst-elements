// Copyright 2009-2025 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2025, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _H_VANADIS_OS_APP_RUNTIME_MEMORY
#define _H_VANADIS_OS_APP_RUNTIME_MEMORY

#include <unordered_set>
#include <stdint.h>
#include <unistd.h>
#include <string>
#include "sst/core/interfaces/stdMem.h"
#include <sst/core/module.h>
#include <sst/core/rng/xorshift.h>


#include "velf/velfinfo.h"
#include "util/vdatacopy.h"
#include "decoder/vauxvec.h"
#include <sst/core/module.h>

#include "sst/elements/mmu/mmu.h"
#include "os/vphysmemmanager.h"
#include "os/vloadpage.h"
#include "os/include/process.h"

namespace SST {
namespace Vanadis {

class AppRuntimeMemoryMod : public Module {
  public:
    SST_ELI_REGISTER_MODULE_API(SST::Vanadis::AppRuntimeMemoryMod)

    AppRuntimeMemoryMod() : Module() {}
    virtual ~AppRuntimeMemoryMod() {}
    virtual uint64_t configurePhdr(  Output* output, int page_size, OS::ProcessInfo*, uint64_t phdr_address, std::vector<uint8_t>& phdr_data_block ) = 0;
    virtual uint64_t configureStack(  Output* output, int page_size, OS::ProcessInfo*, uint64_t stack_top_address,
        uint64_t phdr_address, uint64_t rand_values_address, std::vector<uint8_t>& stack_data ) = 0;
};

template<class Type>
class AppRuntimeMemory : public AppRuntimeMemoryMod {
  public:
    AppRuntimeMemory( Params& params ) { }
    virtual uint64_t configurePhdr(  Output* output, int page_size, OS::ProcessInfo*, uint64_t phdr_address, std::vector<uint8_t>& phdr_data_block );
    virtual uint64_t configureStack(  Output* output, int page_size, OS::ProcessInfo*, uint64_t start_top_address,
        uint64_t phdr_address, uint64_t rand_values_address, std::vector<uint8_t>& stack_data );
};

class AppRuntimeMemory32 : public AppRuntimeMemory<uint32_t> {
  public:
    SST_ELI_REGISTER_MODULE(
        AppRuntimeMemory32,
        "vanadis",
        "AppRuntimeMemory32",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Application runtime memory loader for 32 OS",
        SST::Vanadis::AppRuntimeMemory32
    )
    AppRuntimeMemory32( Params& params ) :
        AppRuntimeMemory<uint32_t>( params ) {}
};

class AppRuntimeMemory64 : public AppRuntimeMemory<uint64_t> {
  public:
    SST_ELI_REGISTER_MODULE(
        AppRuntimeMemory64,
        "vanadis",
        "AppRuntimeMemory64",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Application runtime memory loader for 64b OS",
        SST::Vanadis::AppRuntimeMemory64
    )

    AppRuntimeMemory64( Params& params ) :
        AppRuntimeMemory<uint64_t>( params ) {}
};

template<class Type>
uint64_t AppRuntimeMemory<Type>::configurePhdr(  Output* output, int page_size, OS::ProcessInfo* processInfo, uint64_t phdr_address, std::vector<uint8_t>& phdr_data_block )
{
    auto elf_info = processInfo->getElfInfo();

    for ( int i = 0; i < elf_info->getProgramHeaderEntryCount(); ++i ) {
        const VanadisELFProgramHeaderEntry* nxt_entry = elf_info->getProgramHeader(i);

        vanadis_vec_copy_in<int>(phdr_data_block, (int)nxt_entry->getHeaderTypeNumber());
        if ( 8 == sizeof(Type)  ) {
            vanadis_vec_copy_in<int>(phdr_data_block, (int)nxt_entry->getSegmentFlags());
        }
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getImageOffset());
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getVirtualMemoryStart());
        // Physical address - just ignore this for now
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getPhysicalMemoryStart());
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getHeaderImageLength());
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getHeaderMemoryLength());
        if ( 4 == sizeof(Type) ) {
            vanadis_vec_copy_in<int>(phdr_data_block, (int)nxt_entry->getSegmentFlags());
        }
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getAlignment());
    }
    // Check endian-ness
    if ( elf_info->getEndian() != VANADIS_LITTLE_ENDIAN ) {
        output->fatal(
            CALL_INFO, -1,
            "Error: binary executable ELF information shows this was not compiled for little-endian processors "
            "(\"mipsel\"), please recompile to a supported format.\n");
    }

    phdr_data_block.insert( phdr_data_block.end(), 64, 0 );

    const uint64_t rand_values_address = phdr_address + phdr_data_block.size();

    std::vector<uint8_t>& random_values_data_block = phdr_data_block;

    RNG::XORShiftRNG rng(272727);

    for ( int i = 0; i < 8; ++i ) {
        auto val = rng.generateNextUInt32() % 255;
        random_values_data_block.push_back(val);
    }

    if ( 8 == sizeof( Type ) ) {
        const char*    exe_path            = elf_info->getBinaryPathShort();
        for ( int i = 0; i < std::strlen(exe_path); ++i ) {
            random_values_data_block.push_back(exe_path[i]);
        }

        random_values_data_block.push_back('\0');
    }
    output->verbose(CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT,"phdr_address=%#" PRIx64 " length=%zu\n",phdr_address,phdr_data_block.size());

    // pad to full page
    phdr_data_block.insert( phdr_data_block.end(), page_size - (phdr_data_block.size() % page_size), 0 );

    return rand_values_address;
}


template<class Type>
uint64_t AppRuntimeMemory<Type>::configureStack(  Output* output, int page_size, OS::ProcessInfo* processInfo, uint64_t start_stack_address,
        uint64_t phdr_address, uint64_t rand_values_address, std::vector<uint8_t>& stack_data )
{
    Params& params = processInfo->getParams();
    auto elf_info = processInfo->getElfInfo();
    output->verbose(CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "Application Startup Processing %s bit\n",8 == sizeof(Type)? "64":"32");

    const uint32_t arg_count = params.find<uint32_t>("argc", 1);
    const uint32_t env_count = params.find<uint32_t>("env_count", 0);

    char*                 arg_name = new char[32];
    std::vector<uint8_t>  arg_data_block;
    std::vector<uint64_t> arg_start_offsets;

    for ( uint32_t arg = 0; arg < arg_count; ++arg ) {
        snprintf(arg_name, 32, "arg%" PRIu32 "", arg);
        std::string arg_value = params.find<std::string>(arg_name, "");
        if ( "" == arg_value ) {
            if ( 0 == arg ) {
                arg_value = elf_info->getBinaryPathShort();
                output->verbose(CALL_INFO, 8, VANADIS_OS_DBG_APP_INIT, "--> auto-set \"%s\" to \"%s\"\n", arg_name, arg_value.c_str());
            }
            else {
                output->fatal(
                    CALL_INFO, -1,
                    "Error - unable to find argument %s, value is empty "
                    "string which is not allowed in Linux.\n",
                    arg_name);
            }
        }

        output->verbose(CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "--> Found %s = \"%s\"\n", arg_name, arg_value.c_str());

        uint8_t* arg_value_ptr = (uint8_t*)&arg_value.c_str()[0];

        // Record the start
        arg_start_offsets.push_back((uint64_t)arg_data_block.size());

        for ( size_t j = 0; j < arg_value.size(); ++j ) {
            arg_data_block.push_back(arg_value_ptr[j]);
        }

        arg_data_block.push_back((uint8_t)('\0'));
    }
    delete[] arg_name;

    char*                 env_var_name = new char[32];
    std::vector<uint8_t>  env_data_block;
    std::vector<uint64_t> env_start_offsets;

    for ( uint32_t env_var = 0; env_var < env_count; ++env_var ) {
        snprintf(env_var_name, 32, "env%" PRIu32 "", env_var);
        std::string env_value = params.find<std::string>(env_var_name, "");

        if ( "" == env_value ) {
            output->fatal(
                CALL_INFO, -1,
                "Error - unable to find a value for %s, value is empty "
                "or non-existent which is not allowed.\n",
                env_var_name);
        }

        output->verbose(CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "--> Found %s = \"%s\"\n", env_var_name, env_value.c_str());

        uint8_t* env_value_ptr = (uint8_t*)&env_value.c_str()[0];

        // Record the start
        env_start_offsets.push_back((uint64_t)env_data_block.size());

        for ( size_t j = 0; j < env_value.size(); ++j ) {
            env_data_block.push_back(env_value_ptr[j]);
        }
        env_data_block.push_back((uint8_t)('\0'));
    }

    delete[] env_var_name;

#if 0 // PHDR
    std::vector<uint8_t> phdr_data_block;

    for ( int i = 0; i < elf_info->getProgramHeaderEntryCount(); ++i ) {
        const VanadisELFProgramHeaderEntry* nxt_entry = elf_info->getProgramHeader(i);

        vanadis_vec_copy_in<int>(phdr_data_block, (int)nxt_entry->getHeaderTypeNumber());
        if ( 8 == sizeof(Type)  ) {
            vanadis_vec_copy_in<int>(phdr_data_block, (int)nxt_entry->getSegmentFlags());
        }
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getImageOffset());
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getVirtualMemoryStart());
        // Physical address - just ignore this for now
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getPhysicalMemoryStart());
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getHeaderImageLength());
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getHeaderMemoryLength());
        if ( 4 == sizeof(Type) ) {
            vanadis_vec_copy_in<int>(phdr_data_block, (int)nxt_entry->getSegmentFlags());
        }
        vanadis_vec_copy_in<Type>(phdr_data_block, (Type)nxt_entry->getAlignment());
    }
    // Check endian-ness
    if ( elf_info->getEndian() != VANADIS_LITTLE_ENDIAN ) {
        output->fatal(
            CALL_INFO, -1,
            "Error: binary executable ELF information shows this was not compiled for little-endian processors "
            "(\"mipsel\"), please recompile to a supported format.\n");
    }

    const uint64_t phdr_address = params.find<uint64_t>("program_header_address", 0x60000000);

    phdr_data_block.insert( phdr_data_block.end(), 64, 0 );

    const uint64_t rand_values_address = phdr_address + phdr_data_block.size();

    std::vector<uint8_t>& random_values_data_block = phdr_data_block;

    for ( int i = 0; i < 8; ++i ) {
        random_values_data_block.push_back(rand() % 255);
    }

    if ( 8 == sizeof( Type ) ) {
        const char*    exe_path            = elf_info->getBinaryPath();
        for ( int i = 0; i < std::strlen(exe_path); ++i ) {
            random_values_data_block.push_back(exe_path[i]);
        }

        random_values_data_block.push_back('\0');
    }
#endif // PHDR

    std::vector<uint8_t> aux_data_block;

    // AT_EXECFD (file descriptor of the executable)
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_EXECFD);
    vanadis_vec_copy_in<Type>(aux_data_block, 4);

    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_PHDR);
    vanadis_vec_copy_in<Type>(aux_data_block, (int)phdr_address);

    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_PHENT);
    vanadis_vec_copy_in<Type>(aux_data_block, (int)elf_info->getProgramHeaderEntrySize());

    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_PHNUM);
    vanadis_vec_copy_in<Type>(aux_data_block, (int)elf_info->getProgramHeaderEntryCount());

    // System page size
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_PAGESZ);
    vanadis_vec_copy_in<Type>(aux_data_block, page_size);

    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_ENTRY);
    vanadis_vec_copy_in<Type>(aux_data_block, (int)elf_info->getEntryPoint());

    // AT_BASE (base address loaded into)
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_BASE);
    vanadis_vec_copy_in<Type>(aux_data_block, 0);

    // AT_FLAGS
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_FLAGS);
    vanadis_vec_copy_in<Type>(aux_data_block, 0);

    // AT_HWCAP
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_HWCAP);
    vanadis_vec_copy_in<Type>(aux_data_block, 0);

    // AT_CLKTCK (Clock Tick Resolution)
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_CLKTCK);
    vanadis_vec_copy_in<Type>(aux_data_block, 100);

    // Not ELF
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_NOTELF);
    vanadis_vec_copy_in<Type>(aux_data_block, 0);

    // Real UID
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_UID);
    vanadis_vec_copy_in<Type>(aux_data_block, (Type)processInfo->getuid());

    // Effective UID
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_EUID);
    vanadis_vec_copy_in<Type>(aux_data_block, (Type)processInfo->getuid());

    // Real GID
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_GID);
    vanadis_vec_copy_in<Type>(aux_data_block, (Type)processInfo->getgid());

    // Effective GID
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_EGID);
    vanadis_vec_copy_in<Type>(aux_data_block, (Type)processInfo->getgid());

    // D-Cache Line Size
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_DCACHEBSIZE);
    vanadis_vec_copy_in<Type>(aux_data_block, 64);

    // I-Cache Line Size
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_ICACHEBSIZE);
    vanadis_vec_copy_in<Type>(aux_data_block, 64);

    // AT_SECURE?
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_SECURE);
    vanadis_vec_copy_in<Type>(aux_data_block, 0);

    // AT_RANDOM - 8 bytes of random stuff
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_RANDOM);
    vanadis_vec_copy_in<Type>(aux_data_block, (Type)rand_values_address);

    // AT_EXEFN - path to full executable
    if ( 8 == sizeof(Type) ) {
        vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_EXECFN);
        vanadis_vec_copy_in<Type>(aux_data_block, (Type)(rand_values_address + 16));
    }

    // End the Auxillary vector
    vanadis_vec_copy_in<Type>(aux_data_block, VANADIS_AT_NULL);
    vanadis_vec_copy_in<Type>(aux_data_block, 0);
    // Find out how many AUX entries we added, these should be an int
    // (identifier) and then an int (value) so div by 8 but we need to count
    // ints, so really div by 4
    const int aux_entry_count = aux_data_block.size() / sizeof(Type);

    output->verbose(CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "-> Argument Count:                       %" PRIu32 "\n", arg_count);
    output->verbose(
            CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "---> Data Size for items:                %" PRIu32 "\n",
            (uint32_t)arg_data_block.size());
    output->verbose(
            CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "-> Environment Variable Count:           %" PRIu32 "\n",
            (uint32_t)env_start_offsets.size());
    output->verbose(
            CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "---> Data size for items:                %" PRIu32 "\n",
            (uint32_t)env_data_block.size());
    output->verbose(
            CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "---> Data size of aux-vector:            %" PRIu32 "\n",
            (uint32_t)aux_data_block.size());
    output->verbose(CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "---> Aux entry count:                    %d\n", aux_entry_count);
    output->verbose(
            CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "-> Setting SP to (not-aligned):          %" PRIu64 " / 0x%0" PRI_ADDR "\n", start_stack_address,
            start_stack_address);

    uint64_t arg_env_space_needed = 1 + arg_count + 1 + env_count + 1 + aux_entry_count;
    uint64_t arg_env_space_and_data_needed =
            (arg_env_space_needed * sizeof(Type)) + arg_data_block.size() + env_data_block.size() + aux_data_block.size();

    uint64_t       aligned_start_stack_address = (start_stack_address - arg_env_space_and_data_needed);
    const uint64_t padding_needed              = (aligned_start_stack_address % 64);
    aligned_start_stack_address                = aligned_start_stack_address - padding_needed;

    output->verbose(
            CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT,
            "Aligning stack address to 64 bytes (%" PRIu64 " - %" PRIu64 " - padding: %" PRIu64 " = %" PRIu64
            " / 0x%0" PRI_ADDR ")\n",
            start_stack_address, (uint64_t)arg_env_space_and_data_needed, padding_needed, aligned_start_stack_address,
            aligned_start_stack_address);

    start_stack_address = aligned_start_stack_address;

    const uint64_t arg_env_data_start = start_stack_address + (arg_env_space_needed * sizeof(Type));

    output->verbose(CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "-> Setting start of stack data to:       %#" PRIx64 "\n", arg_env_data_start );

    vanadis_vec_copy_in<Type>(stack_data, arg_count);

    for ( size_t i = 0; i < arg_start_offsets.size(); ++i ) {
        output->verbose(
                CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "--> Setting arg%" PRIu32 " to point to address %" PRIu64 " / 0x%" PRI_ADDR "\n", (uint32_t)i,
                arg_env_data_start + arg_start_offsets[i], arg_env_data_start + arg_start_offsets[i]);
        vanadis_vec_copy_in<Type>(stack_data, (Type)(arg_env_data_start + arg_start_offsets[i]));
    }

    vanadis_vec_copy_in<Type>(stack_data, 0);

    for ( size_t i = 0; i < env_start_offsets.size(); ++i ) {
        output->verbose(
                CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "--> Setting env%" PRIu32 " to point to address %" PRIu64 " / 0x%" PRI_ADDR "\n", (uint32_t)i,
                arg_env_data_start + arg_data_block.size() + env_start_offsets[i],
                arg_env_data_start + arg_data_block.size() + env_start_offsets[i]);

        vanadis_vec_copy_in<Type>(
                stack_data, (Type)(arg_env_data_start + arg_data_block.size() + env_start_offsets[i]));
    }

    vanadis_vec_copy_in<Type>(stack_data, 0);

    for ( size_t i = 0; i < aux_data_block.size(); ++i ) {
        stack_data.push_back(aux_data_block[i]);
    }

    for ( size_t i = 0; i < arg_data_block.size(); ++i ) {
        stack_data.push_back(arg_data_block[i]);
    }

    for ( size_t i = 0; i < env_data_block.size(); ++i ) {
        stack_data.push_back(env_data_block[i]);
    }

    for ( size_t i = 0; i < padding_needed; ++i ) {
        vanadis_vec_copy_in<uint8_t>(stack_data, (uint8_t)0);
    }

    output->verbose(
            CALL_INFO, 16, VANADIS_OS_DBG_APP_INIT, "-> Pushing %" PRIu64 " bytes to the start of stack (0x%" PRI_ADDR ") via memory init event..\n",
            (uint64_t)stack_data.size(), start_stack_address);

#if 0  // PHDR
    output->verbose(CALL_INFO, 16, 0,"phdr_address=%#" PRIx64 " length=%zu\n",phdr_address,phdr_data_block.size());

    // pad to full page
    phdr_data_block.insert( phdr_data_block.end(), page_size - (phdr_data_block.size() % page_size), 0 );

    size_t  phdrRegionStop = phdr_address + phdr_data_block.size();
    // setup a VM memory region for this process
    processInfo->addMemRegion( "phdr", phdr_address, phdrRegionStop - phdr_address, 0x4  );

    // write contents to memory
    loadPages( output, mem_if, mmu, memMgr, processInfo->getPid(), phdr_address, phdr_data_block, 0x4, page_size );
#endif // PHDR

    // get a page aligned base of the stack
    uint64_t page_aligned_stack_addr = start_stack_address & ~(page_size - 1);
    size_t pad = start_stack_address - page_aligned_stack_addr;

    // this results in a reallocation and copy of the vector
    stack_data.insert( stack_data.begin(), pad, 0 );
    stack_data.insert( stack_data.end(), page_size - (stack_data.size() % page_size), 0 );

#if 0
    // stack vm region start right after the phdrs
    uint64_t stackRegionStop = page_aligned_stack_addr + stack_data.size();
    processInfo->addMemRegion( "stack", phdrRegionStop, stackRegionStop - phdrRegionStop, 0x6 );

    output->verbose( CALL_INFO, 16, 0, "stack_address=%#" PRIx64 " aligned_stack_address=%#" PRIx64 " length=%zu\n",
                                                        start_stack_address, page_aligned_stack_addr, stack_data.size());

    loadPages( output, mem_if, mmu, memMgr, processInfo->getPid(), page_aligned_stack_addr, stack_data, 0x6, page_size );

    processInfo->printRegions("after app runtime setup");
#endif
    // Set up the stack pointer
    // Register 29 is MIPS for Stack Pointer
    // regFile->setIntReg(sp_phys_reg, start_stack_address);
    return start_stack_address;
}

} // namespace Vanadis
} // namespace SST
#endif
