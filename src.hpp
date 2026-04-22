#pragma once

#include <vector>
#include <string>
#include <iostream>

class Task {
public:
    Task(std::string name, size_t time, size_t period)
        : name(name), first_interval(time), period(period) {}
    void set() {}
    size_t getFirstInterval() const { return first_interval; }
    size_t getPeriod() const { return period; }
    void execute() { std::cout << "Task: " << name << " excuted" << std::endl; }
    static void incTime() {}
    static size_t getCnt() { return 0; }

private:
    std::string name;
    const size_t first_interval;
    const size_t period;
};

class TimingWheel;
class Timer;

class TaskNode {
    friend class TimingWheel;
    friend class Timer;
public:
    TaskNode(Task* t = nullptr)
        : task(t), next(nullptr), prev(nullptr), rem(0), rounds(0), level(-1), slot(-1), active(false) {}

private:
    Task* task;
    TaskNode* next; TaskNode* prev;
    int rem;      // remaining lower-level seconds when cascading
    int rounds;   // additional full cycles for top wheel (hours)
    int level;    // 0: sec, 1: min, 2: hour, -1: none
    int slot;     // slot index
    bool active;
};

class TimingWheel {
    friend class Timer;
public:
    TimingWheel(size_t size, size_t interval) : size(size), interval(interval), current_slot(0) {
        slots = new TaskNode*[size];
        for (size_t i = 0; i < size; ++i) slots[i] = nullptr;
    }
    ~TimingWheel() { delete [] slots; }

private:
    const size_t size;
    const size_t interval;
    size_t current_slot;
    TaskNode** slots; // heads of doubly-linked lists per slot
};

class Timer {
public:
    Timer() : sec(60, 1), minu(60, 60), hour(24, 3600) {}
    ~Timer() {}

    TaskNode* addTask(Task* task) {
        if (!task) return nullptr;
        TaskNode* node = new TaskNode(task);
        node->active = true;
        insert_node_with_dt(node, (int)task->getFirstInterval());
        return node;
    }

    void cancelTask(TaskNode *p) {
        if (!p || !p->active) return;
        unlink_node(p);
        p->active = false;
    }

    std::vector<Task*> tick() {
        std::vector<Task*> due;

        // advance seconds
        sec.current_slot = (sec.current_slot + 1) % sec.size;

        // on wrap, advance minute and hour, then cascade
        if (sec.current_slot == 0) {
            minu.current_slot = (minu.current_slot + 1) % minu.size;
            if (minu.current_slot == 0) {
                hour.current_slot = (hour.current_slot + 1) % hour.size;
                cascade_from_hour();
            }
            cascade_from_minute();
        }

        // execute tasks in current sec slot
        int s = (int)sec.current_slot;
        TaskNode* p = sec.slots[s];
        sec.slots[s] = nullptr;
        while (p) {
            TaskNode* nxt = p->next;
            p->next = p->prev = nullptr;
            p->slot = -1; p->level = -1;
            if (p->active && p->task) {
                due.push_back(p->task);
                size_t period = p->task->getPeriod();
                if (period > 0) {
                    insert_node_with_dt(p, (int)period);
                } else {
                    p->active = false;
                }
            } else {
                p->active = false;
            }
            p = nxt;
        }
        return due;
    }

private:
    TimingWheel sec;
    TimingWheel minu;
    TimingWheel hour;

    void link_head(TimingWheel& w, int slot, TaskNode* node, int level) {
        node->level = level;
        node->slot = slot;
        node->prev = nullptr;
        node->next = w.slots[slot];
        if (w.slots[slot]) w.slots[slot]->prev = node;
        w.slots[slot] = node;
    }

    void unlink_node(TaskNode* node) {
        if (!node) return;
        TimingWheel* w = nullptr;
        if (node->level == 0) w = &sec;
        else if (node->level == 1) w = &minu;
        else if (node->level == 2) w = &hour;
        if (w && node->slot >= 0) {
            TaskNode** head = &w->slots[node->slot];
            if (node->prev) node->prev->next = node->next;
            if (node->next) node->next->prev = node->prev;
            if (*head == node) *head = node->next;
        }
        node->next = node->prev = nullptr;
        node->slot = -1;
        node->level = -1;
    }

    void insert_node_with_dt(TaskNode* node, int dt) {
        if (!node) return;
        if (dt <= 0) {
            int slot = (int)sec.current_slot;
            link_head(sec, slot, node, 0);
            node->rem = 0; node->rounds = 0;
            return;
        }
        if (dt < (int)sec.size) {
            int slot = (int)((sec.current_slot + (size_t)dt) % sec.size);
            link_head(sec, slot, node, 0);
            node->rem = 0; node->rounds = 0;
            return;
        }
        int sec_per_min = (int)minu.interval; // 60
        if (dt < (int)(minu.size * minu.interval)) {
            int mins = dt / sec_per_min;
            int rems = dt % sec_per_min;
            int slot = (int)((minu.current_slot + (size_t)mins) % minu.size);
            link_head(minu, slot, node, 1);
            node->rem = rems; node->rounds = 0;
            return;
        }
        int sec_per_hour = (int)hour.interval; // 3600
        long long hours_count = dt / sec_per_hour;
        int remh = dt % sec_per_hour;
        int slot_offset = (int)(hours_count % (long long)hour.size);
        int slot = (int)((hour.current_slot + (size_t)slot_offset) % hour.size);
        link_head(hour, slot, node, 2);
        node->rem = remh;
        node->rounds = (int)(hours_count / (long long)hour.size);
    }

    void cascade_from_minute() {
        int slot = (int)minu.current_slot;
        TaskNode* p = minu.slots[slot];
        minu.slots[slot] = nullptr;
        while (p) {
            TaskNode* nxt = p->next;
            p->next = p->prev = nullptr;
            p->slot = -1; p->level = -1;
            insert_node_with_dt(p, p->rem);
            p = nxt;
        }
    }

    void cascade_from_hour() {
        int slot = (int)hour.current_slot;
        TaskNode* p = hour.slots[slot];
        TaskNode* new_head = nullptr;
        while (p) {
            TaskNode* nxt = p->next;
            p->prev = p->next = nullptr;
            if (p->rounds > 0) {
                p->rounds -= 1;
                p->level = 2; p->slot = slot;
                p->next = new_head;
                if (new_head) new_head->prev = p;
                new_head = p;
            } else {
                p->slot = -1; p->level = -1;
                insert_node_with_dt(p, p->rem);
            }
            p = nxt;
        }
        hour.slots[slot] = new_head;
    }
};
