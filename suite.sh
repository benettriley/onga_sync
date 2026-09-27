# ONGA Sync suite manifest. Sourced by the scripts in scripts/ (bash).
#
# To ship a new suite: tag each plug-in repo, bump that plug-in's TAG here, bump
# SUITE_VERSION, then push a tag "v$SUITE_VERSION" to this repo. CI builds every plug-in
# from its pinned tag, assembles the installer and publishes a GitHub Release.

SUITE_NAME="ONGA Sync"
SUITE_VERSION="2026.1"
SUITE_ID="com.ongatools.sync"

# Order here is the order of the checkboxes in the installer.
PLUGINS=(bloom transformer voxmaster mageq panna)

# Each plugin_<name> sets:
#   REPO, TAG      where to build from (a pinned tag, never a branch)
#   TARGET         the juce_add_plugin target (artefacts land in <TARGET>_artefacts)
#   APP            1 if it ships a standalone app
#   CMAKE_ARGS     extra configure flags (switch off tests and dev tools)
#   AUVAL          type subtype manufacturer, for validation in CI
#   BLURB          one line for the installer's checkbox
#   OLD_NAMES      earlier bundle names; preinstall moves these to the Trash
#   OLD_RECEIPTS   earlier installer receipts; preinstall forgets them
# Bundle names are the plug-in name itself (bloom.component, bloom.vst3, bloom.app).
# The plug-in codes never change, so sessions saved with an old name still open.

plugin_bloom() {
    REPO=benettriley/onga_bloom; TAG=v0.1.0; TARGET=OngaBloom; APP=0
    CMAKE_ARGS=(-DONGA_BLOOM_BUILD_HARNESS=OFF)
    AUVAL=(aufx OgBl Onga)
    BLURB="Tape-ceiling bloom, with a reverb fed by what the ceiling shaves off."
    OLD_NAMES=("ONGA BLOOM")
    OLD_RECEIPTS=(com.onga.bloom.pkg)
}

plugin_transformer() {
    REPO=benettriley/onga_transformer; TAG=v0.1.0; TARGET=OngaTransformer; APP=1
    CMAKE_ARGS=(-DONGA_XFMR_BUILD_TESTS=OFF)
    AUVAL=(aufx OgTf Onga)
    BLURB="Classic large-diaphragm mic voicings for a modern mic."
    OLD_NAMES=("ONGA Transformer")
    OLD_RECEIPTS=(com.ongatools.transformer.vst3 com.ongatools.transformer.au com.ongatools.transformer.app
                  com.ongatools.transformer.install.vst3 com.ongatools.transformer.install.au)
}

plugin_voxmaster() {
    REPO=benettriley/voxmaster; TAG=v1.2.0; TARGET=Voxmaster; APP=1
    CMAKE_ARGS=(-DVOXMASTER_BUILD_TESTS=OFF)
    AUVAL=(aufx VxMr Onga)
    BLURB="Vintage vocal channel strip: program EQ, opto, de-esser, echo, reverb."
    OLD_NAMES=("Voxmaster" "Vox Master 3000")
    OLD_RECEIPTS=(com.melodien.voxmaster3000.pkg com.melodien.voxmaster.pkg
                  com.ongatools.voxmaster.pkg com.onga.voxmaster.pkg)
}

plugin_mageq() {
    REPO=benettriley/mageq_onga; TAG=v1.0.0; TARGET=magEQ; APP=1
    CMAKE_ARGS=()
    AUVAL=(aufx Mgeq Onga)
    BLURB="Dynamic tube program EQ modelled on the EQP-1A."
    OLD_NAMES=("magEQ")
    OLD_RECEIPTS=()
}

plugin_panna() {
    REPO=benettriley/panna; TAG=v1.1.0; TARGET=Pannavisio; APP=1
    CMAKE_ARGS=(-DPANNAVISIO_BUILD_TESTS=OFF)
    AUVAL=(aufx Pnvs Onga)
    BLURB="Rhythmic auto-panner: tempo-synced, triggered or manual."
    OLD_NAMES=("PANNA" "PANNAVISIO")
    OLD_RECEIPTS=(com.onga.pannavisio.install com.onga.pannavisio.uninstall)
}

# Clears the per-plugin variables, then loads one plug-in's.
load_plugin() {
    REPO= TAG= TARGET= APP=0 CMAKE_ARGS=() AUVAL=() BLURB= OLD_NAMES=() OLD_RECEIPTS=()
    declare -F "plugin_$1" >/dev/null || { echo "error: unknown plug-in '$1' (see suite.sh)" >&2; return 1; }
    "plugin_$1"
}
