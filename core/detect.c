#include "detect.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* 1 if `exts` (".nes .fds") lists the extension of path, case ignored */
static int ext_listed(const char *exts, const char *path) {
    const char *dot = strrchr(path, '.'), *slash = strrchr(path, '/'), *bslash = strrchr(path, '\\');
    if (!dot || (slash && slash > dot) || (bslash && bslash > dot)) return 0;
    char ext[16];
    size_t n = 0;
    for (; dot[n] && n < sizeof ext - 1; n++) ext[n] = (char)tolower((unsigned char)dot[n]);
    ext[n] = 0;
    for (const char *p = exts; (p = strstr(p, ext)); p += n)
        if ((p == exts || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1;
    return 0;
}

int detect_system(const SystemDetector *const *dets, int n, const uint8_t *data, size_t size,
                  const char *path, char *err, size_t errlen) {
    int best = -1, best_score = 0, tie = -1;
    for (int i = 0; i < n; i++) {
        int s = dets[i]->score(data, size);
        if (s > 0 && path && ext_listed(dets[i]->exts, path)) s += DETECT_EXT_BONUS;
        if (s > best_score) best = i, best_score = s, tie = -1;
        else if (s == best_score && s > 0) tie = i;
    }
    if (best < 0 || best_score < DETECT_MIN) {
        snprintf(err, errlen, "%s: not a game of a known system (use --system)", path ? path : "file");
        return -1;
    }
    if (tie >= 0 && best_score >= DETECT_SURE) {
        snprintf(err, errlen, "%s: could be %s or %s (use --system %s or --system %s)", path ? path : "file",
                 dets[best]->name, dets[tie]->name, dets[best]->id, dets[tie]->id);
        return -1;
    }
    return best;
}
