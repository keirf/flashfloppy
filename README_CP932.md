************************************
** FlashFloppy cp932 日本語 KANJI MOD readme
** Tatsuyuki Sato
************************************

GOTEK FlashFloppyの日本語対応版です。
日本語ファイル名をマウントとOLEDへの漢字表示が可能になります。
OLEDで漢字表示を行うためには、フォントファイルをUSBストレージのルートディレクトリに配置する必要があります。
フォントサイズが6x13では'font1212.bin'、8x16では'font1616.bin'のファイルが使用されいます。

1.スタンドアロンバージョン : standalone verseion w.o.bootloader

全てのGOTEKで動きますが、bootloaderと共存できません。
FF_Gotek-Static-{ver.}.hex : serial update
FF_Gotek-Static-{ver.}.dfu : USB-DFU update

2.ノーマル256KBバージョン : normal 256KB version.

フラッシュメモリ容量が256KB以上のGOTEKだけで動きます。(only FlashMemory 256KB+)
SERIALまたはDFUから更新した場合、bootloaderも256KB対応版になります。

FF_Gotek-{ver.}.cp932.hex  : serial upate
FF_Gotek-{ver.}.cp932.dfu  : USB-DFU udpate

.updでアップデートを行う場合は、先にブートローダーを256KB対応版に更新しておく必要があります。
(update 256KB bootloader first)

FF_Gotek-Reloader-{ver.}.cp932.upd   : reloader
FF_Gotek-Bootloader-{ver.}.cp932.rld : 256KB BootLoader for reloader
FF_Gotek-{ver.}.cp932.upd            : USB storage update (256 BL required)

************************************
**
************************************

フォントのファイルフォーマットはMB831000-042とMB831000-044を結合したもと互換性があります。
フォントファイルが東雲フォントファミリーの.bdfファイルから'font_gen/bdf2mb.exe'を使って生成できます。

漢字のフォントはファイルを表示前に全てRAMにロードされていますが、OLEDの更新割り込みで僅かに遅延が増加します。
それはFDデータのポーリング周期に僅かに影響を及ぼします。

