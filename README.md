# SunsetAsset

アセット (モデル、テクスチャ、音、独自のデータ …) を取り込んで管理するライブラリです。
エンジン (SunsetGraphicsEngine) にも GPU にも依存せず、単独で使えます。SunsetStudio が使います。

> **AI を使った開発について**: このリポジトリは、AI (Anthropic の Claude / Claude Code) を使って作成しています。コード、テスト、ドキュメントに AI が書いたものを含みます。コミットのメッセージに `Co-Authored-By: Claude` が付いているものは、AI と一緒に作成したコミットです。

- アセットは ID (GUID) で識別します。ファイルの隣の `<ファイル>.meta` に ID と取り込みの設定を書くので、
  ファイルを移しても、名前を変えても参照が切れません。
- 取り込んだ結果は自前のバイナリ (版つき) にしてキャッシュし、元のファイルが変わったら作り直します。
- 設計: [docs/design/asset-library.md](docs/design/asset-library.md)

## 状態

| ターゲット | 内容 |
|------|------|
| `SunsetAsset::core` | GUID、`.meta` の読み書き、アセットのデータベース (走査、ID → パス、外部の参照)、取り込みの登録、キャッシュの入れ物 (版つきのバイナリ) |
| `SunsetAsset::model` | モデルの取り込み: FBX / OBJ (ufbx)、glTF / GLB (cgltf)。メッシュ、サブメッシュ、マテリアルのスロット、ノード、スキン、モーフ。右手系 / Y が上 / メートルに変換 |
| `SunsetAsset::texture` | テクスチャの取り込み: PNG / JPEG / TGA / BMP (stb_image)。RGBA8 |

依存 (どれも FetchContent で版と SHA256 を固定して取得): nlohmann/json 3.12 (MIT)、cgltf 1.15 (MIT)、
ufbx 0.23.1 (MIT / 公開領域)、stb_image (MIT / 公開領域)。取り込みのライブラリは中だけで使い、ヘッダーには出しません。

## ビルド

Visual Studio 2022 (C++20) と CMake 3.22 以降。

```
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

| オプション | 既定 | 内容 |
|------|------|------|
| `SUNSET_ASSET_BUILD_TESTS` | OFF (プリセットでは ON) | doctest の単体テスト |
| `SUNSET_ASSET_INSTALL` | ON | `find_package(SunsetAsset)` で使えるようにする |

使う側は `find_package(SunsetAsset 0.1)` か FetchContent で取り込み、必要なターゲット (`SunsetAsset::core` / `model` / `texture`) をリンクします。
