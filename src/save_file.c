#include <efi.h>
#include "exception_context.h"
#include "print.h"
#include "save_file.h"

EFI_FILE_PROTOCOL* output_file = NULL;
EFI_STATUS open_save_file(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
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
        printf(L"Could not open the boot volume, status = 0x%lx\r\n");
        return status;
    }

    status = root_dir->Open(root_dir, &output_file, L"interesting_x86_instructions.bin",
            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);

    root_dir->Close(root_dir);
    root_dir = NULL;


    if (status != EFI_SUCCESS)
    {
        printf(L"Could not open output file, status = 0x%lx\r\n");
    }

    return status;
}

EFI_STATUS save_instruction_data(struct context* context, uint8_t* instruction_bytes, size_t length, uint64_t extra_data)
{
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
    return output_file->Flush(output_file);
}

UINT64 get_save_file_position(void)
{
    UINT64 position = (UINT64)-1;

    output_file->GetPosition(output_file, &position);

    return position;
}

void close_save_file(void)
{
    output_file->Close(output_file);
    output_file = NULL;
}
