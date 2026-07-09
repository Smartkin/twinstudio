#include "archive_serializers.h"
#include "auto_struct_bd_archive.h"
#include "auto_struct_bh_archive.h"
#include "auto_struct_mb_archive.h"
#include "auto_struct_mh_archive.h"
#include "serialization/binary_serializer.h"
#include <stdint.h>


void TwinRes_MbArchiveDeserialize(TwinRes_MbArchive* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    arrsetcap(target->items, target->header.recordsAmount);
    for (uint32_t i = 0; i < target->header.recordsAmount; ++i)
    {
        TwinRes_MhRecord* headerRecord = target->header.records + i;
        headerRecord->interleave = target->header.interleave;
        TwinStudio_BinSerializerSetPosition(deserializer, headerRecord->offset);
        TwinRes_MbRecord record = TwinRes_MbRecordCreate();
        record.header = *headerRecord;
        TwinRes_MbRecordBinDeserialize(&record, deserializer, arena, headerRecord->size, target);
        arrput(target->items, record);
    }
}


void TwinRes_BdArchiveDeserialize(TwinRes_BdArchive* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    uint32_t recordsAmt = arrlen(target->header.records);
    arrsetcap(target->items, recordsAmt);
    for (uint32_t i = 0; i < recordsAmt; ++i)
    {
        TwinRes_BhRecord headerRecord = target->header.records[i];
        TwinStudio_BinSerializerSetPosition(deserializer, headerRecord.offset);
        TwinRes_BdRecord record = TwinRes_BdRecordCreate();
        record.header = headerRecord;
        TwinRes_BdRecordBinDeserialize(&record, deserializer, arena, headerRecord.length, target);
        arrput(target->items, record);
    }
}
