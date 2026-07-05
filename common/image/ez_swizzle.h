#ifndef TS_EZ_SWIZZLE_H
#define TS_EZ_SWIZZLE_H

#include <stdint.h>

void TwinStudio_WriteTexPSMT8(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);
void TwinStudio_ReadTexPSMT8(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);

void TwinStudio_WriteTexPSMCT16(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);
void TwinStudio_ReadTexPSMCT16(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);

void TwinStudio_WriteTexPSMCT32(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);
void TwinStudio_ReadTexPSMCT32(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);


// public static List<Color> TagToColors(GIFTag tag, List<Color> colors)
// {
//     List<UInt64> data = tag.Data.Select(d => d.Output).ToList();
//     for (var i = 0; i < data.Count - 1; i += 2)
//     {
//         UInt64 output1 = data[i + 1];
//         UInt64 output2 = data[i];
//         Color c1 = new Color();
//         Color c2 = new Color();
//         Color c3 = new Color();
//         Color c4 = new Color();
//         c1.FromABGR((UInt32)((output1 >> 0) & 0xFFFFFFFF));
//         c2.FromABGR((UInt32)((output1 >> 32) & 0xFFFFFFFF));
//         c3.FromABGR((UInt32)((output2 >> 0) & 0xFFFFFFFF));
//         c4.FromABGR((UInt32)((output2 >> 32) & 0xFFFFFFFF));
//         colors.Add(c1);
//         colors.Add(c2);
//         colors.Add(c3);
//         colors.Add(c4);
//     }
//     return colors;
// }

// public static GIFTag ColorsToTag(List<Color> colors)
// {
//     GIFTag tag = new GIFTag();
//     tag.NREG = 16;
//     tag.EOP = 1;
//     tag.NLOOP = (ushort)(colors.Count / 4);
//     tag.REGS = new REGSEnum[16];
//     tag.FLG = GIFModeEnum.IMAGE;
//     tag.Data = new List<RegOutput>();
//     for (var i = 0; i < colors.Count - 3; i += 4)
//     {
//         UInt64 col0 = colors[i + 0].ToABGR();
//         UInt64 col1 = colors[i + 1].ToABGR();
//         UInt64 col2 = colors[i + 2].ToABGR();
//         UInt64 col3 = colors[i + 3].ToABGR();
//         UInt64 long1 = (col1 << 32) | (col0);
//         UInt64 long2 = (col3 << 32) | (col2);
//         RegOutput reg1 = new RegOutput();
//         reg1.REG = REGSEnum.HWREG;
//         reg1.Output = long1;
//         RegOutput reg2 = new RegOutput();
//         reg2.REG = REGSEnum.HWREG;
//         reg2.Output = long2;
//         tag.Data.Add(reg2);
//         tag.Data.Add(reg1);
//     }
//     return tag;
// }
// public static byte[] TagToBytes(GIFTag tag)
// {
//     List<UInt64> data = tag.Data.Select(d => d.Output).ToList();
//     byte[] bytes = new byte[data.Count * 8];
//     for (var i = 0; i < data.Count / 2; ++i)
//     {
//         UInt64 output1 = data[i * 2];
//         UInt64 output2 = data[i * 2 + 1];
//         Array.Copy(BitConverter.GetBytes(output2), 0, bytes, i * 16, 8);
//         Array.Copy(BitConverter.GetBytes(output1), 0, bytes, i * 16 + 8, 8);
//     }
//     return bytes;
// }

// public static void ColorsToByte(Color color, byte[] array, int index)
// {
//     UInt32 abrg = color.ToABGR();
//     array[index * 4 + 3] = (Byte)((abrg >> 24) & 0xFF);
//     array[index * 4 + 2] = (Byte)((abrg >> 16) & 0xFF);
//     array[index * 4 + 1] = (Byte)((abrg >> 8) & 0xFF);
//     array[index * 4 + 0] = (Byte)((abrg >> 0) & 0xFF);
// }
// public static Color BytesToColor(byte[] array, int index)
// {
//     Color color = new Color();
//     color.FromABGR((UInt32)((array[index + 3] << 24) | (array[index + 2] << 16) | (array[index + 1] << 8) | (array[index + 0] << 0)));
//     return color;
// }

// public static List<Color> BytesToColors(byte[] array)
// {
//     List<Color> colors = new List<Color>(array.Length / 4);
//     for (var i = 0; i < array.Length / 4; ++i)
//     {
//         colors.Add(BytesToColor(array, i * 4));
//     }

//     return colors;
// }


#endif // TS_EZ_SWIZZLE_H