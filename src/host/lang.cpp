/*
 * Copyright (c) 2026 Jesus Martinez-Mateo
 *
 * Author: Jesus Martinez-Mateo <jesus.martinez.mateo@gmail.com>
 *
 * This file is part of a GPL-licensed project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "host/lang.h"

namespace Lang {

static const char *const strings_en[] = {
  "PC/XT 8086 ESP32-Emulation",
  "Select command",
  "Restart emulator;Reboot machine;Configure;Mount disk;Continue",
  "Reset the board\n\n"
  "Reboot the emulation (Ctrl+Alt+Del)\n\n"
  "Configure emulated computer\n\n"
  "Mount a floppy or hard drive\n\n"
  "Return to emulation",
  "Configuration",
  "Video",
  "CGA (4 colors);EGA (16 colors);Hercules (mono);Tandy (16 colors);MCGA (256 colors)",
  "Sound",
  "None;AdLib sound card",
  "Select drive",
  "A:;B:;C:;D:",
  "Select disk image",
  "Uncompressing disk...",
  "Finished",
  "File uncompressed\nReturn to emulation?",
  "Save configuration and reset?"
};

static const char *const strings_es[] = {
  "PC/XT 8086 ESP32-Emulación",
  "Seleccione una opción",
  "Reiniciar emulador;Reiniciar equipo;Configurar;Montar disco;Continuar",
  "Reinicia la placa (Reset)\n\n"
  "Reinicia el equipo (Ctrl+Alt+Del)\n\n"
  "Configura el equipo emulado\n\n"
  "Montar un disquete o disco duro\n\n"
  "Regresar a la emulación",
  "Configuración",
  "Vídeo",
  "CGA (4 colores);EGA (16 colores);Hercules (mono);Tandy (16 colores);MCGA (256 colores)",
  "Audio",
  "Ninguno;AdLib",
  "Seleccione una unidad",
  "A:;B:;C:;D:",
  "Seleccione un disco de imagen",
  "Descomprimiento disco...",
  "Finalizado",
  "Fichero descomprimido\n¿Regresar a la emulación?",
  "¿Guardar la configuración y reiniciar?"
};

static const char *const *current_table = strings_en;

void set(Id lang)
{
  current_table = (lang == Id::EN) ? strings_en : strings_es;
}

const char *get(Msg msg)
{
  return current_table[(uint16_t) msg];
}

} // end of namespace
