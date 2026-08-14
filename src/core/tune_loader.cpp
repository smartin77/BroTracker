/*
 * BroTracker
 *
 * Description: JSON tune loader for development and UI testing.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#include "tune_loader.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    class JsonParser
    {
    public:
        explicit JsonParser(
            const std::string& text)
            : text_(text)
        {
        }

        Tune ParseTune()
        {
            Tune tune;

            Expect('{');

            while (!Consume('}'))
            {
                const std::string key =
                    ParseString();

                Expect(':');

                if (key == "tune")
                {
                    ParseTuneObject(tune);
                }
                else
                {
                    SkipValue();
                }

                Consume(',');
            }

            return tune;
        }

    private:
        const std::string& text_;
        std::size_t position_ = 0;

        void SkipWhitespace()
        {
            while (
                position_ < text_.size() &&
                std::isspace(
                    static_cast<unsigned char>(
                        text_[position_])))
            {
                ++position_;
            }
        }

        bool Consume(
            char expected)
        {
            SkipWhitespace();

            if (
                position_ < text_.size() &&
                text_[position_] == expected)
            {
                ++position_;
                return true;
            }

            return false;
        }

        void Expect(
            char expected)
        {
            SkipWhitespace();

            if (
                position_ >= text_.size() ||
                text_[position_] != expected)
            {
                throw std::runtime_error(
                    "Invalid JSON format.");
            }

            ++position_;
        }

        std::string ParseString()
        {
            SkipWhitespace();
            Expect('"');

            std::string result;

            while (position_ < text_.size())
            {
                const char character =
                    text_[position_++];

                if (character == '"')
                {
                    return result;
                }

                if (character == '\\')
                {
                    if (position_ >= text_.size())
                    {
                        throw std::runtime_error(
                            "Invalid JSON string.");
                    }

                    const char escaped =
                        text_[position_++];

                    switch (escaped)
                    {
                        case '"':
                            result += '"';
                            break;

                        case '\\':
                            result += '\\';
                            break;

                        case '/':
                            result += '/';
                            break;

                        case 'n':
                            result += '\n';
                            break;

                        case 'r':
                            result += '\r';
                            break;

                        case 't':
                            result += '\t';
                            break;

                        default:
                            throw std::runtime_error(
                                "Unsupported JSON escape.");
                    }
                }
                else
                {
                    result += character;
                }
            }

            throw std::runtime_error(
                "Unterminated JSON string.");
        }

        int ParseInteger()
        {
            SkipWhitespace();

            const std::size_t start =
                position_;

            if (
                position_ < text_.size() &&
                text_[position_] == '-')
            {
                ++position_;
            }

            while (
                position_ < text_.size() &&
                std::isdigit(
                    static_cast<unsigned char>(
                        text_[position_])))
            {
                ++position_;
            }

            if (start == position_)
            {
                throw std::runtime_error(
                    "Expected JSON integer.");
            }

            return std::stoi(
                text_.substr(
                    start,
                    position_ - start));
        }

        void SkipValue()
        {
            SkipWhitespace();

            if (position_ >= text_.size())
            {
                throw std::runtime_error(
                    "Unexpected end of JSON.");
            }

            if (text_[position_] == '"')
            {
                ParseString();
                return;
            }

            if (text_[position_] == '{')
            {
                Expect('{');

                while (!Consume('}'))
                {
                    ParseString();
                    Expect(':');
                    SkipValue();
                    Consume(',');
                }

                return;
            }

            if (text_[position_] == '[')
            {
                Expect('[');

                while (!Consume(']'))
                {
                    SkipValue();
                    Consume(',');
                }

                return;
            }

            ParseInteger();
        }

        void ParseTuneObject(
            Tune& tune)
        {
            Expect('{');

            while (!Consume('}'))
            {
                const std::string key =
                    ParseString();

                Expect(':');

                if (key == "name")
                {
                    tune.title = ParseString();
                }
                else if (key == "tempo")
                {
                    tune.tempo =
                        static_cast<
                            std::uint16_t>(
                                ParseInteger());
                }
                else if (key == "pattern")
                {
                    ParsePatterns(tune);
                }
                else
                {
                    SkipValue();
                }

                Consume(',');
            }
        }

        void ParsePatterns(
            Tune& tune)
        {
            Expect('[');

            while (!Consume(']'))
            {
                Pattern pattern =
                    ParsePattern();

                tune.patterns.push_back(
                    pattern);

                Consume(',');
            }
        }

        Pattern ParsePattern()
        {
            Pattern pattern;

            Expect('{');

            while (!Consume('}'))
            {
                const std::string key =
                    ParseString();

                Expect(':');

                if (key == "number")
                {
                    pattern.number =
                        static_cast<
                            std::uint16_t>(
                                ParseInteger());
                }
                else if (key == "length")
                {
                    pattern.length =
                        static_cast<
                            std::uint8_t>(
                                ParseInteger());
                }
                else if (key == "channel")
                {
                    ParseChannels(pattern);
                }
                else
                {
                    SkipValue();
                }

                Consume(',');
            }

            return pattern;
        }

        void ParseChannels(
            Pattern& pattern)
        {
            Expect('[');

            while (!Consume(']'))
            {
                pattern.channels.push_back(
                    ParseChannel());

                Consume(',');
            }
        }

        Channel ParseChannel()
        {
            Channel channel;

            Expect('{');

            while (!Consume('}'))
            {
                const std::string key =
                    ParseString();

                Expect(':');

                if (key == "row")
                {
                    ParseRows(channel);
                }
                else
                {
                    SkipValue();
                }

                Consume(',');
            }

            return channel;
        }

        void ParseRows(
            Channel& channel)
        {
            Expect('[');

            while (!Consume(']'))
            {
                channel.rows.push_back(
                    ParseEvent());

                Consume(',');
            }
        }

        Event ParseEvent()
        {
            Event event;

            Expect('{');

            while (!Consume('}'))
            {
                const std::string key =
                    ParseString();

                Expect(':');

                if (key == "note")
                {
                    const std::string value =
                        ParseString();

                    event.note =
                        ParseNote(value);
                }
                else if (key == "instrument")
                {
                    event.instrument =
                        static_cast<
                            std::uint8_t>(
                                ParseInteger());
                }
                else
                {
                    SkipValue();
                }

                Consume(',');
            }

            return event;
        }

        Note ParseNote(
            const std::string& value)
        {
            if (value.empty())
            {
                return NOTE_EMPTY;
            }

            if (value == "OFF")
            {
                return NOTE_OFF;
            }

            if (value.size() < 3)
            {
                throw std::runtime_error(
                    "Invalid note value.");
            }

            static constexpr char note_names[] =
                "C-C#D-D#E-F-F#G-G#A-A#B-";

            const char note_letter =
                value[0];

            int semitone = -1;

            switch (note_letter)
            {
                case 'C':
                    semitone = 0;
                    break;

                case 'D':
                    semitone = 2;
                    break;

                case 'E':
                    semitone = 4;
                    break;

                case 'F':
                    semitone = 5;
                    break;

                case 'G':
                    semitone = 7;
                    break;

                case 'A':
                    semitone = 9;
                    break;

                case 'B':
                    semitone = 11;
                    break;

                default:
                    throw std::runtime_error(
                        "Invalid note name.");
            }

            if (
                value.size() >= 3 &&
                value[1] == '#')
            {
                ++semitone;
            }

            const int octave =
                value.back() - '0';

            if (
                octave < 0 ||
                octave > 9)
            {
                throw std::runtime_error(
                    "Invalid note octave.");
            }

            return static_cast<Note>(
                (octave + 1) * 12 +
                semitone);
        }
    };
}

Tune LoadTuneFromJson(
    const std::string& path)
{
    std::ifstream file(path);

    if (!file)
    {
        throw std::runtime_error(
            "Unable to open tune file: " +
            path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    const std::string json =
        buffer.str();

    JsonParser parser(json);

    return parser.ParseTune();
}