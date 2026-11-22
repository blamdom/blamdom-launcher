// SPDX-License-Identifier: GPL-3.0-only
// Standalone self-test for the Blamdom mod catalog logic (no GUI, no full launcher):
//   - BlamdomAPI::matchModFile / modFileVersion / isNewerVersion (pure, header-only)
//   - BlamdomAPI::buildModMetadata + the Packwiz .pw.toml write/read round-trip
//     with the new BLAMDOM provider (real Packwiz.cpp + ModIndex.cpp linked in)
//
//   cmake -S tests/blamdom-catalog -B /tmp/blamdom-catalog-test -DCMAKE_PREFIX_PATH=/opt/qt/6.9.3/gcc_64 \
//     && cmake --build /tmp/blamdom-catalog-test && QT_QPA_PLATFORM=offscreen /tmp/blamdom-catalog-test/selftest
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>

#include "FileSystem.h"
#include "modplatform/blamdom/BlamdomAPI.h"
#include "modplatform/packwiz/Packwiz.h"

using namespace Blamdom;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                             \
    do {                                                        \
        if (cond) {                                             \
            ++g_pass;                                           \
        } else {                                                \
            ++g_fail;                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
        }                                                       \
    } while (0)

// The FileSystem symbols Packwiz.cpp / Json.cpp reference (the real ones are
// part of the full launcher, too heavy to link here).
namespace FS {
bool ensureFilePathExists(const QString& filenamepath)
{
    QFileInfo fi(filenamepath);
    return QDir().mkpath(fi.absolutePath());
}

Result<> write(const QString& /*filename*/, const QByteArray& /*data*/)
{
    return std::unexpected<QString>(QStringLiteral("not available in the self-test"));
}

Result<QByteArray> read(const QString& /*filename*/)
{
    return std::unexpected<QString>(QStringLiteral("not available in the self-test"));
}
}  // namespace FS

// BlamdomAPI.cpp (which owns the real definition) is not linked here either.
const QString BlamdomAPI::PROD_URL = QStringLiteral("https://mine.blamdom.dev");

// Trimmed copy of the live mods.json (real ids/versions/hashes, no bodies).
static const char* kModsJson = R"({
  "mods": [
    {
      "name": "HoloScreen", "id": "holoscreen", "version": "0.7.2", "minecraft": "1.20.1",
      "loader": "fabric", "side": "both", "description": "holographic display",
      "download": "https://mine.blamdom.dev/downloads/mods/holoscreen-0.7.2.jar",
      "sha256": "73d578b2e7a68b2b4f1cb71dd37d1b5ac977b9fc3a6a3d25d2cd43c45679fc73",
      "size_bytes": 670207, "released": "2026-11-15T22:47:00Z"
    },
    {
      "name": "Cozy Quests", "id": "cozyquests", "version": "0.1.7", "minecraft": "1.20.1",
      "loader": "fabric", "side": "both", "description": "quest system",
      "download": "https://mine.blamdom.dev/downloads/mods/cozyquests-0.1.7.jar",
      "sha256": "4656d6a657a04b72ce0b60e25952ad78954179b68a1335a652ed16026f4eb3a7",
      "size_bytes": 2576237, "released": "2026-11-19T00:28:45Z"
    },
    {
      "name": "Cozy Fixes", "id": "cozyfixes", "version": "0.1.1", "minecraft": "1.20.1",
      "loader": "fabric", "side": "client", "description": "resource fix",
      "download": "https://mine.blamdom.dev/downloads/mods/cozyfixes-0.1.1.jar",
      "sha256": "f4db62d566ea35ca3cf5fc88571b573d712f7321e43976c2c3d60c5c7fe5e79f",
      "size_bytes": 1217, "released": "2026-11-16T00:00:00Z"
    },
    {
      "name": "Threadstead", "id": "threadstead", "version": "1.1.0", "minecraft": "1.20.1",
      "loader": "fabric", "side": "both", "description": "steady movement",
      "download": "https://mine.blamdom.dev/downloads/mods/threadstead-1.1.0.jar",
      "sha256": "797ac07c8574f74d197119384c2ee7190e38403460458a92b4d1c30c2893c569",
      "size_bytes": 44576, "released": "2026-11-15T15:04:00Z"
    }
  ]})";

static void write(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
}

int main(int argc, char** argv)
{
    QCoreApplication qtApp(argc, argv);

    const auto mods = BlamdomAPI::parseMods(kModsJson);
    CHECK(mods.size() == 4);
    CHECK(mods[0].id == "holoscreen");
    CHECK(mods[1].id == "cozyquests");
    CHECK(mods[2].id == "cozyfixes");
    CHECK(mods[3].id == "threadstead");
    CHECK(BlamdomAPI::parseMods("<html>nope</html>").isEmpty());

    // ---- version parsing from file names
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "cozyquests-0.1.7.jar") == "0.1.7");
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "COZYQUESTS-0.1.7.JAR") == "0.1.7");
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "cozyquests-0.1.7.jar.disabled") == "0.1.7");
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "cozyquests-1.0-beta.jar") == "1.0-beta");
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "cozyquests0.1.7.jar").isEmpty());   // missing '-'
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "cozyquests-.jar").isEmpty());       // no version
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "cozyquests-0.1.7.zip").isEmpty());  // not a jar
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "cozyquests-0.1.7").isEmpty());      // no extension
    CHECK(BlamdomAPI::modFileVersion("cozyquests", "bebpm-1.2.3.jar").isEmpty());

    // ---- catalog matching
    CHECK(BlamdomAPI::matchModFile(mods, "cozyquests-0.1.7.jar")->id == "cozyquests");
    CHECK(BlamdomAPI::matchModFile(mods, "holoscreen-0.7.2.jar")->id == "holoscreen");
    CHECK(BlamdomAPI::matchModFile(mods, "cozyfixes-0.1.1.jar")->id == "cozyfixes");
    CHECK(BlamdomAPI::matchModFile(mods, "THREADSTEAD-1.1.0.JAR")->id == "threadstead");
    CHECK(BlamdomAPI::matchModFile(mods, "cozyquests-0.1.6.jar.disabled")->id == "cozyquests");  // any version
    CHECK(BlamdomAPI::matchModFile(mods, "bebpm-1.2.3.jar") == nullptr);                         // stock mod, not ours
    CHECK(BlamdomAPI::matchModFile(mods, "randomfile.jar") == nullptr);
    CHECK(BlamdomAPI::matchModFile(mods, "") == nullptr);

    // id-prefix traps: an id that is a prefix of a longer id (or vice versa)
    // must never match, because '-' has to come right after the id.
    {
        QList<Mod> extended = mods;
        Mod prefix;
        prefix.id = "cozy";
        prefix.name = "prefix trap";
        extended.append(prefix);
        CHECK(BlamdomAPI::matchModFile(extended, "cozyquests-0.1.7.jar")->id == "cozyquests");  // not "cozy"
        CHECK(BlamdomAPI::matchModFile(extended, "cozy-9.9.9.jar") && BlamdomAPI::matchModFile(extended, "cozy-9.9.9.jar")->id == "cozy");

        Mod longer;
        longer.id = "cozyquests-x";
        longer.name = "longer trap";
        extended.append(longer);
        CHECK(BlamdomAPI::matchModFile(extended, "cozyquests-0.1.7.jar")->id == "cozyquests");  // not "cozyquests-x"
    }

    // ---- isNewerVersion matrix
    CHECK(BlamdomAPI::isNewerVersion("0.1.7", "0.1.1"));
    CHECK(!BlamdomAPI::isNewerVersion("0.1.1", "0.1.7"));
    CHECK(!BlamdomAPI::isNewerVersion("0.1.7", "0.1.7"));
    CHECK(BlamdomAPI::isNewerVersion("1.0.0", "0.9.9"));
    CHECK(!BlamdomAPI::isNewerVersion("0.9.9", "1.0.0"));
    CHECK(BlamdomAPI::isNewerVersion("0.7.10", "0.7.9"));  // numeric, not lexicographic
    CHECK(!BlamdomAPI::isNewerVersion("0.7.2", "0.7.10"));
    CHECK(BlamdomAPI::isNewerVersion("1.0.1", "1.0"));  // trailing numeric segment
    CHECK(!BlamdomAPI::isNewerVersion("1.0", "1.0.1"));
    CHECK(BlamdomAPI::isNewerVersion("1.0", "1.0-beta"));  // prerelease tail sorts lower
    CHECK(!BlamdomAPI::isNewerVersion("1.0-beta", "1.0"));
    CHECK(BlamdomAPI::isNewerVersion("1.0-beta", "1.0-alpha"));  // lexicographic among tags
    CHECK(!BlamdomAPI::isNewerVersion("1.0-alpha", "1.0-beta"));
    CHECK(BlamdomAPI::isNewerVersion("1.2.0", "1.1.0"));
    CHECK(BlamdomAPI::isNewerVersion("2.0.0", "1.9.9"));
    CHECK(BlamdomAPI::isNewerVersion("0.7.2", "garbage"));  // numeric segment outranks a non-numeric tag
    CHECK(!BlamdomAPI::isNewerVersion("garbage", "0.1.0"));
    CHECK(!BlamdomAPI::isNewerVersion("", "0.1.0"));
    // live-catalog example: threadstead 1.1.0 installed vs 1.2.0 published
    CHECK(BlamdomAPI::isNewerVersion("1.2.0", "1.1.0"));

    // ---- metadata builder
    const auto& cq = mods[1];
    const auto meta = BlamdomAPI::buildModMetadata(cq, "cozyquests-0.1.7.jar");
    CHECK(meta.slug == "cozyquests");
    CHECK(meta.filename == "cozyquests-0.1.7.jar");
    CHECK(meta.provider == ModPlatform::ResourceProvider::BLAMDOM);
    CHECK(meta.projectId.toString() == "cozyquests");
    CHECK(meta.versionNumber == "0.1.7");
    CHECK(meta.hashFormat == "sha256");
    CHECK(meta.hash == "4656d6a657a04b72ce0b60e25952ad78954179b68a1335a652ed16026f4eb3a7");
    CHECK(meta.side == ModPlatform::SideType::UniversalSide);
    CHECK(meta.loaders.testFlag(ModPlatform::ModLoaderType::Fabric));
    CHECK(meta.mcVersions == QStringList{ "1.20.1" });
    CHECK(meta.isValid());
    {
        const auto cfMeta = BlamdomAPI::buildModMetadata(mods[2], "cozyfixes-0.1.1.jar");
        CHECK(cfMeta.side == ModPlatform::SideType::ClientSide);  // side: "client"
    }

    // ---- pack index identity (findInstalled's detection input)
    // Raw strings use a "json" delimiter: the bodies contain ')', and a bare
    // R"(...)" ends at the first ')' + '"'.
    {
        QString n, v;
        // real shape: name + versionId
        CHECK(BlamdomAPI::indexIdentity(
                  QJsonDocument::fromJson(R"json({"formatVersion":1,"game":"minecraft","name":"Our Cozy Pack","versionId":"0.3.1-beta.3"})json")
                      .object(),
                  n, v));
        CHECK(n == "Our Cozy Pack");
        CHECK(v == "0.3.1-beta.3");
        // hand-made index: no versionId is legal (update flow works without it)
        CHECK(BlamdomAPI::indexIdentity(QJsonDocument::fromJson(R"json({"name":"Cozy (working title)"})json").object(), n, v));
        CHECK(n == "Cozy (working title)");
        CHECK(v.isEmpty());
        // whitespace trimmed
        CHECK(BlamdomAPI::indexIdentity(QJsonDocument::fromJson(R"json({"name":  "  Padded Name  ", "versionId":" 1.0 "})json").object(), n, v));
        CHECK(n == "Padded Name");
        CHECK(v == "1.0");
        // missing / empty name: not a pack index
        CHECK(!BlamdomAPI::indexIdentity(QJsonDocument::fromJson(R"json({"versionId":"1.0"})json").object(), n, v));
        CHECK(!BlamdomAPI::indexIdentity(QJsonDocument::fromJson(R"json({"name":"  "})json").object(), n, v));
        CHECK(!BlamdomAPI::indexIdentity(QJsonDocument::fromJson(R"json({"name":42})json").object(), n, v));

        // name normalization keeps early installs matching after the name settled
        CHECK(BlamdomAPI::normalizePackName("Our Cozy Pack (working title)") == "our cozy pack");
        CHECK(BlamdomAPI::normalizePackName("  OUR COZY PACK ") == "our cozy pack");
        CHECK(BlamdomAPI::normalizePackName("Our Cozy Pack") == "our cozy pack");
    }

    // ---- write/read round-trip (real Packwiz code)
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString indexDir = tmp.path() + "/.index";

    auto m2 = BlamdomAPI::buildModMetadata(cq, "cozyquests-0.1.7.jar");
    Packwiz::V1::updateModIndex(QDir(indexDir), m2);
    CHECK(QFile::exists(indexDir + "/cozyquests.pw.toml"));

    auto readBack = Packwiz::V1::getIndexForMod(QDir(indexDir), "cozyquests");
    CHECK(readBack.isValid());
    CHECK(readBack.provider == ModPlatform::ResourceProvider::BLAMDOM);
    CHECK(readBack.modId().toString() == "cozyquests");
    CHECK(readBack.filename == "cozyquests-0.1.7.jar");
    CHECK(readBack.name == "Cozy Quests");
    CHECK(readBack.versionNumber == "0.1.7");
    CHECK(readBack.hashFormat == "sha256");
    CHECK(readBack.hash == "4656d6a657a04b72ce0b60e25952ad78954179b68a1335a652ed16026f4eb3a7");
    CHECK(readBack.side == ModPlatform::SideType::UniversalSide);
    CHECK(readBack.loaders.testFlag(ModPlatform::ModLoaderType::Fabric));

    // version bump in place: rewriting the same slug points at the new version
    auto m3 = BlamdomAPI::buildModMetadata(cq, "cozyquests-0.1.7.jar");
    m3.fileId = "0.1.8";
    m3.versionNumber = "0.1.8";
    Packwiz::V1::updateModIndex(QDir(indexDir), m3);
    auto readBack2 = Packwiz::V1::getIndexForMod(QDir(indexDir), "cozyquests");
    CHECK(readBack2.versionNumber == "0.1.8");
    CHECK(readBack2.provider == ModPlatform::ResourceProvider::BLAMDOM);

    // a hand-written [update.blamdom] table (the string mapping) reads back as BLAMDOM
    write(indexDir + "/handwritten.pw.toml", R"(
name = "Threadstead"
filename = "threadstead-1.1.0.jar"
side = "both"
x-prismlauncher-loaders = ["fabric"]
x-prismlauncher-mc-versions = ["1.20.1"]
x-prismlauncher-release-type = "Release"
x-prismlauncher-version-number = "1.1.0"
x-prismlauncher-dependencies = []
x-prismlauncher-lock-update = false

[download]
mode = "url"
url = "https://mine.blamdom.dev/downloads/mods/threadstead-1.1.0.jar"
hash-format = "sha256"
hash = "797ac07c8574f74d197119384c2ee7190e38403460458a92b4d1c30c2893c569"

[update.blamdom]
mod-id = "threadstead"
version = "1.1.0"
)");
    auto hand = Packwiz::V1::getIndexForMod(QDir(indexDir), "handwritten");
    CHECK(hand.isValid());
    CHECK(hand.provider == ModPlatform::ResourceProvider::BLAMDOM);
    CHECK(hand.modId().toString() == "threadstead");

    // stock providers still round-trip (no regression from the new enum value)
    {
        Packwiz::V1::Mod mr;
        mr.slug = "stockmod";
        mr.name = "Stock";
        mr.filename = "stockmod-1.0.0.jar";
        mr.mode = "url";
        mr.hashFormat = "sha512";
        mr.hash = "deadbeef";
        mr.provider = ModPlatform::ResourceProvider::MODRINTH;
        mr.projectId = "mrSlug";
        mr.fileId = "12345";
        mr.versionNumber = "1.0.0";
        mr.lockUpdate = false;
        Packwiz::V1::updateModIndex(QDir(indexDir), mr);
        auto mrBack = Packwiz::V1::getIndexForMod(QDir(indexDir), "stockmod");
        CHECK(mrBack.isValid());
        CHECK(mrBack.provider == ModPlatform::ResourceProvider::MODRINTH);
        CHECK(mrBack.modId().toString() == "mrSlug");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
