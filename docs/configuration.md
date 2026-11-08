[← README](../README.md)

# 設定項目

## 設定ファイル

初回の読込後プラグイン本体と同じフォルダに`External_Audio_Processing2.ini`が生成されます。  
設定ファイルをリセットする場合はAviUtl ExEdit2終了後に削除するか、  
AviUtl ExEdit2上部の設定内のプラグイン設定にある`EAP2の設定をリセット`を実行してください。  
設定ファイルの変更はAviUtl ExEdit2の再起動後に反映されます。  
一部設定はAviUtl ExEdit2上部の設定内のプラグイン設定にある`EAP2の設定を再読込`を実行すると再起動なしでも反映されます。

```ini
; デフォルト設定(v0.0.40時点)

; システムによって利用される設定(編集は非推奨)
[Info]
; 設定ファイルが作成されたバージョン(ファイルの更新時には変更されません)
Version=v0.0.40
; 設定ファイルが作成されたバージョンを読み込みやすくしたもの
VersionData=0000000000280000
; 設定ファイルそのもののバージョン
ConfigVersion=1
; 破壊的変更通知用のバージョン
AckedBreakingVersion=0000000000270062
; プラグインの全体に影響する設定
[General]
; 0にすると自動のオブジェクト名変更が有効になる
AutoRenameDisable=1
; 1にすると実験的機能が有効になる
EnableExperimental=0
; 1にすると圧縮した状態で外部プラグインの状態を保存する
CompressPluginState=1
; 1にすると保存時にルートシーン以外の際通知します
; CompatのCheckSaveSceneを0にした場合機能しません
NotifySaveWarn=0
; 1にすると投げ込み機能を無効化します
DisableDropin=0
; モジュールの設定
[Module]
; 1にするとメインフィルタ以外の全てのツールを無効化する
AllToolDisable=0
; 1にするとメインフィルタを無効化する
HostDisable=0
; 1にするとChain系のツールを無効化する
ChainToolDisable=0
; 1にするとメインフィルタ(フィルタオブジェクト)を無効化する
HostFilterDisable=0
; 1にするとメインフィルタ(メディアオブジェクト)を無効化する
HostMediaDisable=0
; 以下は対応するツールを1にすると無効化する
AutoWahDisable=0
ChainCompDisable=0
ChainDynamicEQDisable=0
ChainFilterDisable=0
ChainGateDisable=0
ChainSendDisable=0
DeEsserDisable=0
DistortionDisable=0
DynamicsDisable=0
EQDisable=0
GeneratorDisable=0
MaximizerDisable=0
ModulationDisable=0
NotesSendDisable=0
PhaserDisable=0
PitchShiftDisable=0
ReverbDisable=0
SpatialDisable=0
SpectralGateDisable=0
StereoDisable=0
UtilityDisable=0
MIDIGeneratorDisable=0
AnalyzerDisable=0
; 互換性維持に関する設定
[Compat]
; 新フィルタを利用する際は1に
; 過去のプロジェクトを利用する際は0にしてください
UseNewReverb=0
UseNewGenerator=0
; v0.0.32で追加されたシーンチェックで不具合が発生した際は0にしてください。
CheckSaveScene=1
; 相性問題通知を無効化します
DisableCompatibilityIssueNotify=0
; VSTプラグインに関する設定
[VST]
; 1にすると全てのVSTプラグインで強制的なウィンドウのリサイズができるようになる
; Issue #1の問題が発生した際に1にすると改善すると思います
ForceResize=0
; HostでMap5~8を使えるようにします
; 4つで足りない時に使ってください。
HostAdditionalParameters=1
; アナライザーに関する設定
[Analyzer]
; 目標 Integrated LUFS
TargetLUFS=-14.0
; 目標 True Peak [dBTP]
TargetPeak=-1.0
; 無音判定しきい値 [dBFS]
Silentdb=-60.0
; 無音最短継続 [秒]
SilentMinSec=0.5
; PASS 判定の許容幅 [LU] (±)
LUFSTolerance=1.0
; LUFS FAIL判定条件(target + lufs_fail < lufs)
LUFSFAIL=2.0
; LUFS WARN判定条件(target - lufs_warn >= lufs)
LUFSWARN=8.0
; TP FAIL判定条件(target + peak_fail < peak)
PEAKFAIL=1.0
; 処理サイズ
; 大きいほど処理が高速になりますが、UI反映が遅れます
; 0以下の場合デフォルト値(512)
; 64~8192の間でない場合クランプ
; 2^xで表せる値でない場合は最も近い値で処理されます
BatchSize=512
; ジャンプした問題箇所に自動でチェックをつける
AutoCheck=1
; 問題箇所の再測定時に曖昧一致を利用する
FuzzyMatchIssues=1
; 上記機能で利用する許容範囲を設定する
FuzzyMatchDist=30
; v0.0.39で追加されたオブジェクトからの推定を行う
UseObjectParam=1
; 大きい文字のフォントサイズ(1未満でstyle.confのControlフォントサイズ*1.5)
FontL=24
; 小さい文字のフォントサイズ(1未満でstyle.confのControlフォントサイズ)
FontS=16
; v0.0.39bで追加されたstyle.confからの配色の取得を行う
UseHostColor=0
; 配色を上書きする
ColorBackground=
ColorPanel=
ColorControl=
ColorBorder=
ColorText=
ColorTextSub=
ColorPass=
ColorWarn=
ColorFail=
ColorAccent=
ColorShortTerm=
ColorMomentary=
ColorGraphBackground=
ColorGraphGrid=
ColorGraphLabel=
ColorProgress=
; 実験的機能(EnableExperimental=1のときのみ反映)
[Experimental]
; 現状設定項目無し
```
