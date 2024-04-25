#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <assert.h>

class Stats {
    std::string _name;
    std::vector<double> _values;
    bool _sorted = false;

    void sort() {
        if(!_sorted) {
            std::sort(_values.begin(), _values.end());
            _sorted = true;
        }
    }

public:
    Stats(): _name("") {}
    Stats(std::string name): _name(name) {}

    void add(double x) {
        _sorted = false;
        _values.push_back(x);
    }

    double num_values() const {
        return _values.size();
    }

    double avg() const {
        assert(_values.size() > 0);
        return std::accumulate(_values.begin(), _values.end(), 0.0) / _values.size();
    }

    double stddev() const {
        double avg = this->avg();
        double sumdiff = 0.0;
        for(const double& x : _values) {
            sumdiff += (x - avg) * (x - avg);
        }
        return sqrt(sumdiff / (_values.size() - 1));
    }

    double percentile(double p) {
        assert(p >= 0 && p <= 1);
        assert(_values.size() > 0);

        sort();

        int idx = round(_values.size() * p);
        return _values[idx];
    }

    double median() {
        return percentile(0.5);
    }

    friend std::ostream& operator<<(std::ostream& os, const Stats& s) {
        os << "Stats for " << s._name << ": n=" << s.num_values() << ", avg=" << s.avg() << ", stddev=" << s.stddev();
        return os;
    }

};

class Timer {
    using clock = std::chrono::high_resolution_clock;
    using time_point_t = std::chrono::time_point<clock>;

    std::string _name;
    time_point_t _start;
    Stats *_stats;

public:
    Timer(std::string name, Stats* stats): _name(name), _start(clock::now()), _stats(stats) {}
    Timer(std::string name) : Timer(name, nullptr) {}
    ~Timer() {
        time_point_t end = clock::now();
        double duration = std::chrono::duration<double>(end - _start).count();
        if(_stats) {
            _stats->add(duration);
        } else {
            std::cout<<"Timer " << _name << ": " << duration << "ms" << std::endl;
        }
    }
};

class Counter {
    std::string _name;
    long _count;
    Stats *_stats;

public:
    Counter(std::string name, Stats* stats): _name(name), _count(0), _stats(stats) {}
    Counter(std::string name): _name(name), _count(0), _stats(nullptr) {}

    void add() {
        ++_count;
    }

    void add_if(bool p) {
        if(p)
            ++_count;
    }

    ~Counter() {
        if(_stats) {
            _stats->add(_count);
        } else {
            std::cout<<"Counter " << _name << " occured " << _count << "times" << std::endl;
        }
    }
};