#ifndef SRC_STATUS_HPP_
#define SRC_STATUS_HPP_

class Status {
public:
    Status(uint64_t index, int x, int y, char key):
        index(index),
        x(x),
        y(y),
        key(key){}

    uint64_t getIndex() {
        return index;
    }

    friend std::ostream& operator<<(std::ostream&, const Status&);
private:
    uint64_t index;
    int x;
    int y;
    char key;
};

std::ostream& operator<<(std::ostream& ost, const Status& s) {
    return ost << s.x << "," << s.y << "," << s.key;
}

#endif /* SRC_STATUS_HPP_ */
