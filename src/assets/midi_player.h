// midi_player.h
#ifndef MIDI_PLAYER_H
#define MIDI_PLAYER_H

int midi_init(const char* soundfont_path);
int midi_play(const char* midi_path);
void midi_stop(void);
void midi_shutdown(void);
int midi_is_playing(void);

#endif // MIDI_PLAYER_H
