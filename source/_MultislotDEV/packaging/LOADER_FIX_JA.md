# 1.5.5-loaderfix：同梱ローダーの競合修正

EDFModLoader の `winmm.dll` に、複数スレッドが同時に呼ぶと別の関数へ飛び得る不具合がありました。
180個のWindows関数への転送口が1つの呼び出し先変数を共有していたためです。
各転送口が自分の関数へ直接飛ぶように修正しました。ローダーの他の処理は同じです。

旧版で誤配送が起きる割込み順序をテストで再現し、修正版では同じ経路に入らないこと、
180個の転送先、整数・浮動小数の引数と戻り値、8スレッド計80万回の呼び出しを検証しました。
人数、ビークル、敵の処理に今回新しい変更はありません。
MultiSlot本体はrecovery2の修正を含み、起動ログにローダーの転送方式を記録します。

## 9/22の2件のクラッシュについて

- (33)のログはビークル搭乗時のクラッシュ位置を記録しておらず、原因未確定です。
- もう一方はEOSが `timeEndPeriod(1)` を呼んだ先で `winmmbase.dll` のアクセス違反が記録されています。
  今回見つかった誤配送と整合しますが、当時の実際の転送先や他のメモリ破壊までは記録されていません。
- **修正したローダーの不具合は再現確認済みですが、この2件や同時離脱が実機で解消するかは未確認です。**

## 導入・VRMOD同梱

ゲーム終了後、ZIPの **`winmm.dll` と `Mods/Plugins/EDF6MultiSlot.dll` の両方**を置き換えてください。
友人全員に同じ組み合わせを配布してください。INIは同梱せず、既存設定を維持します。

VRMODの同梱担当へ：従来と同じローダーではありません。MultiSlot本体だけ差し替えると、
今回の競合修正は入りません。VR側パッケージャが旧ローダーのハッシュを固定している場合は、
以下の修正版へ更新し、旧版を上書き同梱しないようにしてください。
EDF6VR.dll、EDF.dll、EOSSDKはこのパッケージでは交換しません。

- 修正版winmm.dll SHA256: `BE94E1FAC0CA12C41B6924E2EB168851641C999CE951D2A5A9FAEA5161B0F9A3`
- 元版 SHA256: `B80E4DA6AE7264F0E9774C992DE3C5F9B6E9BC9422146ADEEB5BB2BD681E112E`
- EDFModLoaderのライセンスは同梱 `EDFModLoader_LICENSE.txt`。
- 変更箇所の全記録は `loader-fix.json`。元版との相違は180転送口の合計2,160バイトのみです。

更新後の `EDF6MultiSlot.log` の最後の起動に、次が出ることを確認できます。

```text
==== EDF6MultiSlot 1.5.5-loaderfix ====
LOADER timeBeginPeriod proxy=direct-register
LOADER timeEndPeriod proxy=direct-register
LOADER PlaySoundW proxy=direct-register
```

`shared-dispatch (legacy)` は旧ローダー、`other` / `unavailable` はこの方法で識別できない状態です。
この3行はその3関数のメモリ上の転送方式を示します。配布ファイル全体の同一性はSHA256で確認します。

再発時は、落ちたPCの **MultiSlotとVRの両方のログ**を再起動前にコピーしてください。
ビークル名、搭乗席、兵科、時刻もあると追跡しやすくなります。
ログの `EXCEPTION` は最初に通知された例外の記録で、必ずしも未処理で終了した証拠ではありません。
強制終了や一部の異常終了では、クラッシュ位置を残せないことがあります。
