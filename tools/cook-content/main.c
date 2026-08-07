/*******************************************************************************************
*
*   cook-content — fast C packager for hex-magical
*
*   Scans playable act-<n>/map-<m>.tmx maps under resources/, follows external
*   tileset / image dependencies, and copies only that closure into dist/content/
*   (plus required runtime sprites and solutions). Authoring junk stays in git.
*
*   Usage: cook-content [resources_dir] [out_dir]
*   Defaults: resources → dist/content
*
********************************************************************************************/

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_PATH 1024
#define MAX_FILES 4096
#define MAX_XML (16 * 1024 * 1024)

static char gFiles[MAX_FILES][MAX_PATH];
static int gFileCount = 0;

static int CmpPath(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static bool PathExists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool IsDir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static void Die(const char *msg)
{
    fprintf(stderr, "cook-content: %s\n", msg);
    exit(1);
}

static void DiePath(const char *msg, const char *path)
{
    fprintf(stderr, "cook-content: %s: %s (%s)\n", msg, path, strerror(errno));
    exit(1);
}

static void NormalizeSlashes(char *path)
{
    for (char *p = path; *p; p++)
    {
        if (*p == '\\') *p = '/';
    }
}

// Collapse "." / ".." segments. path is relative (no leading '/').
static void CollapsePath(char *path)
{
    char stack[64][MAX_PATH];
    int top = 0;
    char work[MAX_PATH];
    snprintf(work, sizeof(work), "%s", path);
    NormalizeSlashes(work);

    char *save = NULL;
    for (char *tok = strtok_r(work, "/", &save); tok != NULL; tok = strtok_r(NULL, "/", &save))
    {
        if (tok[0] == '\0' || strcmp(tok, ".") == 0) continue;
        if (strcmp(tok, "..") == 0)
        {
            if (top > 0) top--;
            continue;
        }
        if (top >= 64) Die("path too deep");
        snprintf(stack[top], MAX_PATH, "%s", tok);
        top++;
    }

    path[0] = '\0';
    for (int i = 0; i < top; i++)
    {
        if (i == 0) snprintf(path, MAX_PATH, "%s", stack[i]);
        else
        {
            size_t n = strlen(path);
            snprintf(path + n, MAX_PATH - n, "/%s", stack[i]);
        }
    }
}

static bool AddFile(const char *relPath)
{
    char norm[MAX_PATH];
    snprintf(norm, sizeof(norm), "%s", relPath);
    NormalizeSlashes(norm);
    CollapsePath(norm);
    if (norm[0] == '\0') return false;

    for (int i = 0; i < gFileCount; i++)
    {
        if (strcmp(gFiles[i], norm) == 0) return true;
    }
    if (gFileCount >= MAX_FILES) Die("too many files in dependency set");
    snprintf(gFiles[gFileCount], MAX_PATH, "%s", norm);
    gFileCount++;
    return true;
}

static char *LoadText(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if ((sz < 0) || (sz > MAX_XML)) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static bool MkDirP(const char *path)
{
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    NormalizeSlashes(tmp);
    size_t len = strlen(tmp);
    if (len == 0) return false;
    for (size_t i = 1; i < len; i++)
    {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        if (!IsDir(tmp) && (mkdir(tmp, 0755) != 0) && (errno != EEXIST)) return false;
        tmp[i] = '/';
    }
    if (!IsDir(tmp) && (mkdir(tmp, 0755) != 0) && (errno != EEXIST)) return false;
    return true;
}

static void DirName(const char *path, char *out, size_t outSize)
{
    snprintf(out, outSize, "%s", path);
    NormalizeSlashes(out);
    char *slash = strrchr(out, '/');
    if (slash == NULL) { snprintf(out, outSize, "."); return; }
    if (slash == out) { out[1] = '\0'; return; }
    *slash = '\0';
}

static void JoinRootRel(const char *root, const char *rel, char *out, size_t outSize)
{
    if (strcmp(rel, ".") == 0 || rel[0] == '\0')
        snprintf(out, outSize, "%s", root);
    else
        snprintf(out, outSize, "%s/%s", root, rel);
    NormalizeSlashes(out);
}

// Resolve source relative to fromRelDir (under resources root) → collapsed rel path.
static void ResolveRel(const char *fromRelDir, const char *source, char *outRel, size_t outSize)
{
    char joined[MAX_PATH];
    if (strcmp(fromRelDir, ".") == 0)
        snprintf(joined, sizeof(joined), "%s", source);
    else
        snprintf(joined, sizeof(joined), "%s/%s", fromRelDir, source);
    NormalizeSlashes(joined);
    CollapsePath(joined);
    snprintf(outRel, outSize, "%s", joined);
}

static bool ExtractAttr(const char *tag, const char *attr, char *out, size_t outSize)
{
    const char *end = strchr(tag, '>');
    const char *p = strstr(tag, attr);
    if ((p == NULL) || ((end != NULL) && (p > end))) return false;
    p += strlen(attr);
    const char *q = strchr(p, '"');
    if ((q == NULL) || ((size_t)(q - p) >= outSize)) return false;
    memcpy(out, p, (size_t)(q - p));
    out[q - p] = '\0';
    return true;
}

// Returns false if the tileset file is missing / unreadable.
static bool CollectTsxDeps(const char *root, const char *tsxRel)
{
    char abs[MAX_PATH];
    JoinRootRel(root, tsxRel, abs, sizeof(abs));
    if (!PathExists(abs))
    {
        fprintf(stderr, "cook-content: skip missing tileset %s\n", tsxRel);
        return false;
    }
    char *xml = LoadText(abs);
    if (xml == NULL)
    {
        fprintf(stderr, "cook-content: cannot read tileset %s\n", tsxRel);
        return false;
    }

    char tsxDir[MAX_PATH];
    DirName(tsxRel, tsxDir, sizeof(tsxDir));

    const char *p = xml;
    while ((p = strstr(p, "<image")) != NULL)
    {
        char source[512] = { 0 };
        if (ExtractAttr(p, "source=\"", source, sizeof(source)))
        {
            if (strncmp(source, ":/", 2) == 0) { p += 6; continue; } // Tiled internal
            char rel[MAX_PATH];
            ResolveRel(tsxDir, source, rel, sizeof(rel));
            AddFile(rel);
        }
        p += 6;
    }
    free(xml);
    return true;
}

// Returns false when the map has a broken external tileset dependency.
static bool CollectTmxDeps(const char *root, const char *tmxRel)
{
    char abs[MAX_PATH];
    JoinRootRel(root, tmxRel, abs, sizeof(abs));
    char *xml = LoadText(abs);
    if (xml == NULL)
    {
        fprintf(stderr, "cook-content: cannot read map %s\n", tmxRel);
        return false;
    }

    char tmxDir[MAX_PATH];
    DirName(tmxRel, tmxDir, sizeof(tmxDir));

    // Stage deps; only commit into gFiles if every external tileset resolves.
    char staged[512][MAX_PATH];
    int stagedCount = 0;
    snprintf(staged[stagedCount++], MAX_PATH, "%s", tmxRel);

    bool ok = true;
    const char *p = xml;
    while ((p = strstr(p, "<tileset")) != NULL)
    {
        char source[512] = { 0 };
        if (ExtractAttr(p, "source=\"", source, sizeof(source)))
        {
            if (strncmp(source, ":/", 2) == 0) { p += 8; continue; }
            char rel[MAX_PATH];
            ResolveRel(tmxDir, source, rel, sizeof(rel));
            char tsxAbs[MAX_PATH];
            JoinRootRel(root, rel, tsxAbs, sizeof(tsxAbs));
            if (!PathExists(tsxAbs))
            {
                fprintf(stderr, "cook-content: skip map %s (missing %s)\n", tmxRel, rel);
                ok = false;
                break;
            }
            if (stagedCount >= 512) Die("too many staged deps for one map");
            snprintf(staged[stagedCount++], MAX_PATH, "%s", rel);
            // Collect image deps into global set only after map commits —
            // call CollectTsxDeps later for committed maps.
        }
        p += 8;
    }

    if (ok)
    {
        p = xml;
        while ((p = strstr(p, "<imagelayer")) != NULL)
        {
            const char *layerEnd = strstr(p, "</imagelayer>");
            const char *img = strstr(p, "<image");
            if ((img != NULL) && ((layerEnd == NULL) || (img < layerEnd)))
            {
                char source[512] = { 0 };
                if (ExtractAttr(img, "source=\"", source, sizeof(source))
                    && (strncmp(source, ":/", 2) != 0))
                {
                    char rel[MAX_PATH];
                    ResolveRel(tmxDir, source, rel, sizeof(rel));
                    if (stagedCount >= 512) Die("too many staged deps for one map");
                    snprintf(staged[stagedCount++], MAX_PATH, "%s", rel);
                }
            }
            p += 11;
        }
    }
    free(xml);

    if (!ok) return false;

    for (int i = 0; i < stagedCount; i++)
    {
        AddFile(staged[i]);
        size_t n = strlen(staged[i]);
        if ((n > 4) && (strcmp(staged[i] + n - 4, ".tsx") == 0))
        {
            if (!CollectTsxDeps(root, staged[i])) return false;
        }
    }
    return true;
}

static bool IsPlayableMapName(const char *name)
{
    if (strncmp(name, "map-", 4) != 0) return false;
    const char *p = name + 4;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
    return strcmp(p, ".tmx") == 0;
}

static bool IsActDirName(const char *name)
{
    if (strncmp(name, "act-", 4) != 0) return false;
    char *end = NULL;
    (void)strtol(name + 4, &end, 10);
    return (end != name + 4) && (*end == '\0');
}

static void ScanPlayableMaps(const char *root)
{
    DIR *d = opendir(root);
    if (d == NULL) DiePath("cannot open resources", root);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;
        if (!IsActDirName(ent->d_name)) continue;
        char actPath[MAX_PATH];
        JoinRootRel(root, ent->d_name, actPath, sizeof(actPath));
        if (!IsDir(actPath)) continue;

        DIR *ad = opendir(actPath);
        if (ad == NULL) continue;
        struct dirent *me;
        while ((me = readdir(ad)) != NULL)
        {
            if (!IsPlayableMapName(me->d_name)) continue;
            char rel[MAX_PATH];
            snprintf(rel, sizeof(rel), "%s/%s", ent->d_name, me->d_name);
            (void)CollectTmxDeps(root, rel);
        }
        closedir(ad);
    }
    closedir(d);
}

static void AddFixedRuntimeFiles(void)
{
    AddFile("spritesheet/isolated/sun.png");
    AddFile("spritesheet/isolated/moon.png");
    AddFile("spritesheet/isolated/cloud-1.png");
    AddFile("spritesheet/isolated/cloud-2.png");
}

static void AddSolutions(const char *root)
{
    char solDir[MAX_PATH];
    JoinRootRel(root, "solutions", solDir, sizeof(solDir));
    if (!IsDir(solDir)) return;
    DIR *d = opendir(solDir);
    if (d == NULL) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        size_t n = strlen(ent->d_name);
        if (n < 10) continue;
        if (strcmp(ent->d_name + n - 9, ".solution") != 0) continue;
        char rel[MAX_PATH];
        snprintf(rel, sizeof(rel), "solutions/%s", ent->d_name);
        AddFile(rel);
    }
    closedir(d);
}

static void CopyFile(const char *src, const char *dst)
{
    char dstDir[MAX_PATH];
    DirName(dst, dstDir, sizeof(dstDir));
    if (!MkDirP(dstDir)) DiePath("mkdir failed", dstDir);

    FILE *in = fopen(src, "rb");
    if (in == NULL) DiePath("cannot read", src);
    FILE *out = fopen(dst, "wb");
    if (out == NULL) { fclose(in); DiePath("cannot write", dst); }

    char buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            fclose(in);
            fclose(out);
            DiePath("write failed", dst);
        }
    }
    fclose(in);
    fclose(out);
}

static void RmTree(const char *path)
{
    char cmd[MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    (void)system(cmd);
}

int main(int argc, char **argv)
{
    const char *root = (argc >= 2) ? argv[1] : "resources";
    const char *outRoot = (argc >= 3) ? argv[2] : "dist/content";

    if (!IsDir(root)) DiePath("resources dir missing", root);

    gFileCount = 0;
    ScanPlayableMaps(root);
    AddFixedRuntimeFiles();
    AddSolutions(root);

    qsort(gFiles, (size_t)gFileCount, MAX_PATH, CmpPath);

    RmTree(outRoot);
    if (!MkDirP(outRoot)) DiePath("cannot create out dir", outRoot);

    int copied = 0;
    int missing = 0;
    for (int i = 0; i < gFileCount; i++)
    {
        char src[MAX_PATH], dst[MAX_PATH];
        JoinRootRel(root, gFiles[i], src, sizeof(src));
        JoinRootRel(outRoot, gFiles[i], dst, sizeof(dst));
        if (!PathExists(src))
        {
            fprintf(stderr, "cook-content: MISSING %s\n", gFiles[i]);
            missing++;
            continue;
        }
        CopyFile(src, dst);
        copied++;
    }

    char manifestPath[MAX_PATH];
    JoinRootRel(outRoot, "manifest.json", manifestPath, sizeof(manifestPath));
    FILE *mf = fopen(manifestPath, "wb");
    if (mf == NULL) DiePath("cannot write manifest", manifestPath);
    fprintf(mf, "{\n  \"source\": \"%s\",\n  \"files\": [\n", root);
    for (int i = 0; i < gFileCount; i++)
    {
        fprintf(mf, "    \"%s\"%s\n", gFiles[i], (i + 1 < gFileCount) ? "," : "");
    }
    fprintf(mf, "  ],\n  \"count\": %d\n}\n", gFileCount);
    fclose(mf);

    fprintf(stderr, "cook-content: copied %d files → %s (%d missing)\n",
            copied, outRoot, missing);
    if (missing > 0) return 2;
    if (copied == 0) Die("no files copied");
    return 0;
}
