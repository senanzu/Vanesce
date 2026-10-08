#pragma once
#include <vector>
#include <string>

enum class NoteType
{
    NOTE,
    HOLD,
    SLIDE
};

struct Note
{
    int lane;
    double timeStamp;
    NoteType type;
    double holdDuration;
};

struct Chart
{
    std::vector<Note> noteList;
    double bpm = 120.0;
};
