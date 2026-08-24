"""Shared authoring values used when building regional species records."""


def rgb565(hex_color: str) -> int:
    red = int(hex_color[1:3], 16)
    green = int(hex_color[3:5], 16)
    blue = int(hex_color[5:7], 16)
    return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)


# Background biome IDs are part of the regional pack data shape.
TYPE_BIOME = {
    "agua": 1, "planta": 2, "bicho": 2, "fuego": 3,
    "roca": 4, "tierra": 4, "dragon": 1, "hielo": 5,
    "normal": 0, "electrico": 0, "lucha": 0, "veneno": 0,
    "psiquico": 0, "fantasma": 0, "acero": 4, "siniestro": 2,
    "hada": 0, "volador": 0,
}

# Marine fossils use the beach rather than the generic rock biome.
BIOME_OVERRIDE = {138: 1, 139: 1, 140: 1, 141: 1}
