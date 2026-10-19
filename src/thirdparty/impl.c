/* ============================================================
 *  src/thirdparty/impl.c
 *
 *  ヘッダー 1 つのライブラリの実装をここで 1 回だけ作る (警告を厳しくしない別のターゲット sasset_thirdparty)。
 *    cgltf (MIT)                 glTF 2.0 / GLB
 *    stb_image (MIT / 公開領域)   PNG / JPEG / TGA / BMP
 *  ufbx (MIT / 公開領域) は ufbx.c をそのままコンパイルする。
 * ============================================================ */
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_NO_STDIO   /* ファイルは自前で読む (UTF-8 のパスのため) */
#include <stb_image.h>
