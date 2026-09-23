#include "NetworkIO.h"

#include <fstream>

using namespace std;

namespace netio {

static const char* kMagic = "SOCIALGRAPH 1";

static void appendClean(string& out, const string& value, char semicolonReplacement = ';') {
    for (char c : value) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        else if (c == ';') c = semicolonReplacement;
        out += c;
    }
}

Result save(const SocialNetwork& network, const filesystem::path& path) {
    ofstream file(path, ios::binary | ios::trunc);
    if (!file) {
        return {false, "Could not open the file for writing."};
    }

    string buffer;
    buffer.reserve(1 << 20);
    auto flush = [&](bool force) {
        if (force || buffer.size() > (1u << 20)) {
            file.write(buffer.data(), (streamsize)buffer.size());
            buffer.clear();
        }
    };

    buffer += kMagic;
    buffer += '\n';

    network.forEachUser([&](const User& u) {
        buffer += "U\t";
        buffer += to_string(u.id);
        buffer += '\t';
        appendClean(buffer, u.username);
        buffer += '\t';
        appendClean(buffer, u.name);
        buffer += '\t';
        buffer += to_string(u.age);
        buffer += '\t';
        bool first = true;
        for (const auto& interest : u.interests) {
            if (interest.empty() || interest == "None Provided") continue;
            if (!first) buffer += ';';
            appendClean(buffer, interest, ',');
            first = false;
        }
        buffer += '\n';
        flush(false);
    });

    for (int id = 1; id <= network.maxUserId(); ++id) {
        for (int friendId : network.getFriends(id)) {
            if (id < friendId) {
                buffer += "E\t";
                buffer += to_string(id);
                buffer += '\t';
                buffer += to_string(friendId);
                buffer += '\n';
            }
        }
        flush(false);
    }
    flush(true);

    if (!file.good()) {
        return {false, "Writing the file failed (is the disk full?)."};
    }
    return {};
}

// Splits `line` on tabs into at most `maxFields` fields (the last keeps the rest).
static size_t splitTabs(const char* begin, const char* end, const char** fields, size_t* lengths, size_t maxFields) {
    size_t count = 0;
    const char* start = begin;
    for (const char* p = begin; p <= end && count < maxFields; ++p) {
        if (p == end || (*p == '\t' && count + 1 < maxFields)) {
            fields[count] = start;
            lengths[count] = (size_t)(p - start);
            ++count;
            start = p + 1;
        }
    }
    return count;
}

static bool parseInt(const char* s, size_t len, long long& out) {
    if (len == 0 || len > 18) return false;
    long long v = 0;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '-') { neg = true; i = 1; if (len == 1) return false; }
    for (; i < len; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

Result load(SocialNetwork& network, const filesystem::path& path) {
    ifstream file(path, ios::binary);
    if (!file) {
        return {false, "Could not open the file."};
    }
    file.seekg(0, ios::end);
    streamoff size = file.tellg();
    file.seekg(0, ios::beg);
    string data;
    if (size > 0) {
        data.resize((size_t)size);
        file.read(&data[0], size);
        if (!file) {
            return {false, "Reading the file failed."};
        }
    }

    SocialNetwork loaded;
    vector<pair<int, int>> edges;
    size_t pos = 0, lineNo = 0;
    bool sawHeader = false;

    auto fail = [&](const string& why) {
        return Result{false, "Line " + to_string(lineNo) + ": " + why};
    };

    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == string::npos) eol = data.size();
        const char* begin = data.data() + pos;
        const char* end = data.data() + eol;
        if (end > begin && end[-1] == '\r') --end;
        pos = eol + 1;
        ++lineNo;

        if (begin == end || *begin == '#') continue;

        if (!sawHeader) {
            if (string(begin, end) != kMagic) {
                return {false, "This is not a Social Graph network file."};
            }
            sawHeader = true;
            continue;
        }

        const char* f[6];
        size_t len[6];
        if (*begin == 'U') {
            size_t n = splitTabs(begin, end, f, len, 6);
            if (n < 5) return fail("expected U <id> <username> <name> <age> [interests]");
            long long id = 0, age = 0;
            if (!parseInt(f[1], len[1], id) || id <= 0 || id > 2000000000) return fail("invalid user id");
            if (!parseInt(f[4], len[4], age)) return fail("invalid age");
            User user((int)id, string(f[2], len[2]), string(f[3], len[3]), (int)age, {});
            if (n == 6) {
                const char* s = f[5];
                const char* e = f[5] + len[5];
                while (s < e) {
                    const char* sep = s;
                    while (sep < e && *sep != ';') ++sep;
                    if (sep > s) user.interests.emplace_back(s, sep);
                    s = sep + 1;
                }
            }
            if (!loaded.restoreUser(user)) return fail("duplicate user id or username");
        } else if (*begin == 'E') {
            size_t n = splitTabs(begin, end, f, len, 3);
            long long a = 0, b = 0;
            if (n != 3 || !parseInt(f[1], len[1], a) || !parseInt(f[2], len[2], b)) {
                return fail("expected E <id> <id>");
            }
            edges.emplace_back((int)a, (int)b);
        } else {
            return fail("unknown record type");
        }
    }

    if (!sawHeader) {
        return {false, "The file is empty."};
    }

    loaded.addConnectionsBulk(edges);
    network = std::move(loaded);
    return {};
}

}  // namespace netio
