// life_probe.h — objeto que registra construções e destruições.
#ifndef GITQUEST_LIFE_PROBE_H
#define GITQUEST_LIFE_PROBE_H

#include <string>
#include <vector>

namespace gq {

struct LifeProbe {
    static int& live() {
        static int n = 0;
        return n;
    }
    // Ordem em que os ids foram destruídos.
    static std::vector<int>& destroyed() {
        static std::vector<int> v;
        return v;
    }

    explicit LifeProbe(int id) : id_(id) { ++live(); }
    LifeProbe(const LifeProbe&) = delete;
    LifeProbe& operator=(const LifeProbe&) = delete;
    ~LifeProbe() {
        --live();
        destroyed().push_back(id_);
    }

    int id_;
};

inline std::string join(const std::vector<int>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i != 0) {
            s += ", ";
        }
        s += std::to_string(v[i]);
    }
    return s + "]";
}

}  // namespace gq

#endif
