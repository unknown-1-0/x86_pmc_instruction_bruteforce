#include <efi.h>
#include "exception_context.h"
#include "print.h"
#include "save_file.h"

EFI_FILE_PROTOCOL* output_file = NULL;
EFI_STATUS open_save_file(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
#ifdef NO_SAVE
    print(L"No output mode enabled, skipping save file creation\r\n");
    return EFI_SUCCESS;
#endif
    EFI_GUID loaded_image_protocol_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    EFI_LOADED_IMAGE_PROTOCOL* loaded_image_protocol = NULL;

    EFI_STATUS status = SystemTable->BootServices->OpenProtocol(ImageHandle, &loaded_image_protocol_guid,
            (VOID**)&loaded_image_protocol, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not open loaded image protocol, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    EFI_HANDLE device_handle = loaded_image_protocol->DeviceHandle;

    status = SystemTable->BootServices->CloseProtocol(ImageHandle, &loaded_image_protocol_guid, ImageHandle, NULL);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not close loaded image protocol, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    EFI_GUID simple_fs_protocol_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* simple_fs_protocol = NULL;

    status = SystemTable->BootServices->OpenProtocol(device_handle, &simple_fs_protocol_guid,
            (VOID**)&simple_fs_protocol, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not open simple FS protocol, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    EFI_FILE_PROTOCOL* root_dir = NULL;

    status = simple_fs_protocol->OpenVolume(simple_fs_protocol, &root_dir);
    SystemTable->BootServices->CloseProtocol(device_handle, &simple_fs_protocol_guid, ImageHandle, NULL);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not open the boot volume, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    status = root_dir->Open(root_dir, &output_file,
#if MODE == 64
            L"interesting_x86_instructions_64bit.bin",
#elif MODE == 32
            L"interesting_x86_instructions_32bit.bin",
#else
#error Unknown CPU mode
#endif
            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);

    root_dir->Close(root_dir);
    root_dir = NULL;


    if (status != EFI_SUCCESS)
    {
        printf(L"Could not open output file, status = 0x%lx\r\n", (uint64_t)status);
    }

    UINTN info_size = 0;
    EFI_GUID file_info_id = EFI_FILE_INFO_ID;

    status = output_file->GetInfo(output_file, &file_info_id, &info_size, NULL);

    if (status != EFI_BUFFER_TOO_SMALL)
    {
        printf(L"Unexpected status when retrieving output file info size: 0x%lx (expected: EFI_BUFFER_TOO_SMALL (0x%lx))\r\n",
                (uint64_t)status, (uint64_t)EFI_BUFFER_TOO_SMALL);
        return status;
    }

    EFI_FILE_INFO* file_info = NULL;
    status = SystemTable->BootServices->AllocatePool(EfiLoaderData, info_size, (void**)&file_info);

    if (status != EFI_SUCCESS)
    {
        printf(L"Memory allocation for output file info failed, status = 0x%lx\r\n", status);
        return status;
    }

    status = output_file->GetInfo(output_file, &file_info_id, &info_size, file_info);

    if (status != EFI_SUCCESS)
    {
        printf(L"Unexpected status when retrieving output file info: 0x%lx (expected: EFI_SUCCESS (0x%lx))\r\n",
                (uint64_t)status, (uint64_t)EFI_SUCCESS);
    }
    else if (file_info->FileSize != 0)
    {
        print(L"Output file is not empty, resetting file size to 0.\r\n");
        file_info->FileSize = 0;

        status = output_file->SetInfo(output_file, &file_info_id, info_size, file_info);

        if (status != EFI_SUCCESS)
        {
            printf(L"Unexpected status when updating output file size: 0x%lx (expected: EFI_SUCCESS (0x%lx))\r\n",
                    (uint64_t)status, (uint64_t)EFI_SUCCESS);
        }
    }

    SystemTable->BootServices->FreePool(file_info);
    file_info = NULL;
    return status;
}
  
EFI_STATUS save_data(void* data, size_t size)
{
    return output_file->Write(output_file, &size, data);
}

EFI_STATUS save_instruction_data(struct context* context, uint8_t* instruction_bytes, size_t length, uint64_t extra_data)
{
#ifdef NO_SAVE
    return EFI_SUCCESS;
#endif
    // Exception number does not exceed 0x20, so this is safe to do to save space
    uint8_t exception_number = (uint8_t)context->exception_number;
    UINTN write_size = sizeof(exception_number); 

    EFI_STATUS status = output_file->Write(output_file, &write_size, &exception_number);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not write exception number, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    if (exception_number & EXCEPTIONS_WITH_ERROR_CODE_MASK)
    {
        write_size = sizeof(context->error_code);
        status = output_file->Write(output_file, &write_size, &context->error_code);

        if (status != EFI_SUCCESS)
        {
            printf(L"Could not write exception error code, status = 0x%lx\r\n", (uint64_t)status);
            return status;
        }
    }

    write_size = sizeof(extra_data);
    status = output_file->Write(output_file, &write_size, &extra_data);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not write extra info, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    // Instructions are at most sizeof(instruction_bytes) in length, which is way less than 0x100,
    // so this is safe to do to save space
    uint8_t instruction_length = (uint8_t)length;

    write_size = sizeof(instruction_length);
    status = output_file->Write(output_file, &write_size, &instruction_length);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not write instruction length, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    write_size = length;
    status = output_file->Write(output_file, &write_size, instruction_bytes);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not write instruction bytes, status = 0x%lx\r\n", (uint64_t)status);
    }

    return EFI_SUCCESS;
}

EFI_STATUS flush_save_file(void)
{
#ifndef NO_SAVE
    return output_file->Flush(output_file);
#else
    return EFI_SUCCESS;
#endif
}

UINT64 get_save_file_position(void)
{
#ifndef NO_SAVE
    UINT64 position = (UINT64)-1;

    output_file->GetPosition(output_file, &position);

    return position;
#else
    return 0;
#endif
}

void close_save_file(void)
{
#ifndef NO_SAVE
    output_file->Close(output_file);
    output_file = NULL;
#endif
}
