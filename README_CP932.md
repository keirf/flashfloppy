************************************
** FlashFloppy cp932 MOD readme
** Tatsuyuki Sato
************************************

GOTEK FlashFloppyの日本語対応版です。
フラッシュメモリ容量が256KB以上のMCUで動きます。容量が128KBのMCUでは動きません。
東雲フォントから'font_gen/bdf2mb.exe'を使ってフォントファイルを生成して、6x13の場合は'font1212.bin'を、8x16の場合は'font1616.bin'をUSBストレージのルートディレクトリに配置します。
256KB版のブートローダーが必要なので、ファームウェアの更新する前にブートローダーを256KB対応に変更してください。
FDDエミュレーション中はフォントファイルをアクセスしませんが、OLEDの描画遅延が多少増加するため、データアクセスのポーリング周期にも多少の影響が出ます。

This is the GOTEK FlashFloppy with the code page cp932 modification.
It can mount Japanese-KANJI filename with no error.
Also this can display KANJI font in OLED.

It supports only MCU with 256KBytes of Flash memory.
because OEM and UTF16 conversion tabel are too big.
The firmware update is possible with 256KB bootloader.
First,You need to update the boot loader to 256KB version with the reloader first.

The KANJI font pattern is dinamicaly read from the USB storage.
The font file is required in the root directory to display KANJI to OLED,'font1212.bin' for 6x13 mode,or 'font1616.bin' for 8x16 mode.
The font files can also be generated from SHINONOME-FONT using 'font_gen/bdf2mb.exe'.
It data format is MB832001-042,MB831000-042 plus 044,or combined MB83256-19 to 26.

The KANJI fonts are loaded into RAM before displaying file names.
I do not access the font file during emulation, but the latency of I2C interrupt handling time slightly increases.
So,it gives a slight delay to the data access polling interval.

