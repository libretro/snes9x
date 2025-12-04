#ifndef LIBRETRO_CORE_OPTIONS_INTL_H__
#define LIBRETRO_CORE_OPTIONS_INTL_H__

#if defined(_MSC_VER) && (_MSC_VER >= 1500 && _MSC_VER < 1900)
/* https://support.microsoft.com/en-us/kb/980263 */
#pragma execution_character_set("utf-8")
#pragma warning(disable : 4566)
#endif

#include <libretro.h>

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
 */

#ifdef __cplusplus
extern "C"
{
#endif

   /*
    ********************************
    * Core Option Definitions
    ********************************
    */

   /* RETRO_LANGUAGE_JAPANESE */

   /* RETRO_LANGUAGE_FRENCH */

   struct retro_core_option_v2_category option_cats_fr[] = {
       {"hacks",
        NULL,
        NULL},
       {"lightgun",
        NULL,
        NULL},
       {"advanced_av",
        NULL,
        NULL},
       {NULL, NULL, NULL},
   };

   struct retro_core_option_v2_definition option_defs_fr[] = {
       {"snes9x_region",
        "Région de la console (rechargement du core)",
        NULL,
        "Indique la région du système. 'PAL' = 50 Hz, 'NTSC' = 60 Hz. Si la région est incorrecte, les jeux tourneront plus vite ou plus lentement que la normale.",
        NULL,
        NULL,
        {
            {"auto", "Automatique"},
            {"ntsc", "NTSC"},
            {"pal", "PAL"},
            {NULL, NULL},
        },
        NULL},
       {"snes9x_aspect",
        "Ratio d'aspect préféré",
        NULL,
        "Sélectionnez le ratio d'aspect préféré du contenu. Ceci n'est appliqué que si RetroArch a le ratio d'aspect réglé sur 'Par le core' dans les paramètres Vidéo.",
        NULL,
        NULL,
        {
            {"4:3", "4:3"},
            {"4:3 scaled", "4:3 (Conservé)"},
            {"uncorrected", "Non corrigé"},
            {"auto", "Automatique"},
            {"ntsc", "NTSC"},
            {"pal", "PAL"},
            {NULL, NULL},
        },
        NULL},
       {"snes9x_overscan",
        "Découper l'overscan",
        NULL,
        "Supprime les bordures en haut et en bas de l'écran, comme sur une télévision en définition standard. 'Automatique (~8 pixels)' tentera de détecter et recadrer l'overscan selon le contenu.",
        NULL,
        NULL,
        {
            {"enabled", "~8 pixels"},
            {"12_pixels", "12 pixels"},
            {"16_pixels", "16 pixels"},
            {"auto", "Automatique (~8 pixels)"},
            {"disabled", NULL},
            {NULL, NULL},
        },
        NULL},
       {"snes9x_hires_blend",
        "Mélange Hi-Res",
        NULL,
        "Mélange les pixels lorsque le jeu passe en mode Hi-Res (512x448). Nécessaire pour certains jeux utilisant le mode Hi-Res pour produire des effets de transparence (Kirby's Dream Land, Jurassic Park...).",
        NULL,
        NULL,
        {
            {"disabled", NULL},
            {"merge", "Fusion"},
            {"blur", "Flou"},
            {NULL, NULL},
        },
        NULL},
       {"snes9x_blargg",
        "Filtre NTSC Blargg",
        NULL,
        "Applique un filtre vidéo qui imite divers signaux TV NTSC.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_audio_interpolation",
        "Interpolation audio",
        NULL,
        "Applique le filtre audio indiqué. 'Gaussian' reproduit le son avec des basses proches du matériel d'origine. 'Cubic' et 'Sinc' sont moins fidèles mais conservent davantage la dynamique.",
        NULL,
        NULL,
        {
            {"gaussian", "Gaussian"},
            {"cubic", "Cubic"},
            {"sinc", "Sinc"},
            {"none", "Aucun"},
            {"linear", "Linéaire"},
            {NULL, NULL},
        },
        NULL},
       {"snes9x_up_down_allowed",
        "Autoriser les directions opposées",
        NULL,
        "Permet d'appuyer / changer rapidement / maintenir en même temps les directions gauche et droite (ou haut et bas). Cela peut provoquer des comportements indésirables.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_overclock_superfx",
        "Surcadençage SuperFX",
        NULL,
        "Multiplicateur de fréquence pour le processeur SuperFX. Peut augmenter le framerate ou provoquer des problèmes de timing. Des valeurs inférieures à 100% peuvent améliorer les performances sur des appareils lents.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_overclock_cycles",
        "Réduire les ralentissements (Hack, non sécurisé)",
        "Réduire les ralentissements (non sécurisé)",
        "Overclock du CPU SNES. Peut provoquer des plantages ! Utilisez 'Light' pour des temps de chargement réduits, 'Compatible' pour la plupart des jeux montrant des ralentissements, et 'Max' uniquement si nécessaire (Gradius 3, Super R Type...).",
        NULL,
        NULL,
        {
            {"disabled", "Désactivé"},
            {"light", "Léger"},
            {"compatible", "Compatible"},
            {"max", "Max"},
            {NULL, NULL},
        },
        NULL},
       {"snes9x_reduce_sprite_flicker",
        "Réduire le scintillement des sprites (Hack, non sécurisé)",
        "Réduire le scintillement des sprites (non sécurisé)",
        "Augmente le nombre de sprites pouvant être dessinés simultanément à l'écran.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_randomize_memory",
        "Randomiser la mémoire (non sécurisé)",
        NULL,
        "Remplit la RAM système de données aléatoires au démarrage. Certains jeux (p. ex. Super Off Road) utilisent la RAM comme source aléatoire pour le placement d'objets et le comportement de l'IA.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_block_invalid_vram_access",
        "Bloquer les accès VRAM invalides",
        NULL,
        "Certaines homebrews/ROM nécessitent que cette option soit désactivée pour fonctionner correctement.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_echo_buffer_hack",
        "Hack du tampon d'écho (non sécurisé, activer uniquement pour l'ancien addmusic)",
        NULL,
        "Certaines homebrews/ROM nécessitent que cette option soit désactivée pour fonctionner correctement.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_show_lightgun_settings",
        "Afficher les paramètres du pistolet",
        NULL,
        "Active la configuration des entrées pour Super Scope / Justifier / M.A.C.S. REMARQUE : le menu rapide doit être ouvert pour que ce réglage prenne effet.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_lightgun_mode",
        "Mode du pistolet",
        "Mode",
        "Utiliser l'entrée 'Light Gun' contrôlée par la souris ou l'entrée 'Écran tactile'.",
        NULL,
        NULL,
        {
            {"Lightgun", "Light Gun"},
            {"Touchscreen", "Écran tactile"},
            {NULL, NULL},
        },
        NULL},
       {"snes9x_superscope_reverse_buttons",
        "Inverser les boutons Super Scope",
        NULL,
        "Intervertit les positions des boutons 'Fire' et 'Cursor' du Super Scope.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_superscope_crosshair",
        "Réticule Super Scope",
        NULL,
        "Modifier le curseur affiché à l'écran.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_superscope_color",
        "Couleur Super Scope",
        NULL,
        "Changer la couleur du viseur à l'écran.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_justifier1_crosshair",
        "Curseur Justifier 1",
        NULL,
        "Modifier la taille du curseur affiché à l'écran.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_justifier1_color",
        "Couleur Justifier 1",
        NULL,
        "Changer la couleur du viseur à l'écran.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_justifier2_crosshair",
        "Curseur Justifier 2",
        NULL,
        "Modifier la taille du curseur affiché à l'écran.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_justifier2_color",
        "Couleur Justifier 2",
        NULL,
        "Changer la couleur du viseur à l'écran.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_rifle_crosshair",
        "Visée M.A.C.S.",
        NULL,
        "Modifier la visée affichée à l'écran.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_rifle_color",
        "Couleur du pistolet M.A.C.S.",
        NULL,
        "Changer la couleur du viseur.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_show_advanced_av_settings",
        "Afficher les paramètres audio/vidéo avancés",
        NULL,
        "Active la configuration des paramètres de bas niveau du calque vidéo / effets GFX / canaux audio. REMARQUE : le menu rapide doit être ouvert pour que ce réglage soit effectif.",
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_layer_1",
        "Afficher la couche 1",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_layer_2",
        "Afficher la couche 2",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_layer_3",
        "Afficher la couche 3",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_layer_4",
        "Afficher la couche 4",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_layer_5",
        "Afficher la couche des sprites",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_gfx_clip",
        "Activer le clipping des fenêtres graphiques",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_gfx_transp",
        "Activer les effets de transparence",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_sndchan_1",
        "Activer le canal audio 1",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_sndchan_2",
        "Activer le canal audio 2",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_sndchan_3",
        "Activer le canal audio 3",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_sndchan_4",
        "Activer le canal audio 4",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_sndchan_5",
        "Activer le canal audio 5",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_sndchan_6",
        "Activer le canal audio 6",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_sndchan_7",
        "Activer le canal audio 7",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {"snes9x_sndchan_8",
        "Activer le canal audio 8",
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {NULL, NULL},
        },
        NULL},
       {NULL, NULL, NULL, NULL, NULL, NULL, {{NULL, NULL}}, NULL},
   };

   struct retro_core_options_v2 options_fr = {
       option_cats_fr,
       option_defs_fr};

   /* RETRO_LANGUAGE_SLOVAK */

   /* RETRO_LANGUAGE_PERSIAN */

   /* RETRO_LANGUAGE_HEBREW */

   /* RETRO_LANGUAGE_ASTURIAN */

   /* RETRO_LANGUAGE_FINNISH */

#ifdef __cplusplus
}
#endif

#endif
