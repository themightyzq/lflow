// lflow_ui_snapshot: render the real plugin editor headlessly to a PNG.
//
//   lflow_ui_snapshot <out.png> [scale] [width height]   (scale defaults to 2.0; width/height
//                                                          default to the editor's own default
//                                                          size, 700x620 -- pass 620 560 to
//                                                          check the minimum resize floor)
//
// The look-and-feel regression gate for the ZQ SFX house-UI migration (see
// docs/ZQSFX_UI_STYLE_GUIDE.md and docs/ui_migration_report.md): render before a UI change,
// render after, compare. Mirrors the equivalent UI snapshot tool from a sibling ZQ SFX project,
// but links against LFlOw's own shared-code CMake target (the "pamplejuce pattern") instead of
// recompiling the plugin sources a second time -- see CMakeLists.txt for the target wiring.
//
// Determinism: nothing here ever pumps JUCE's message loop (no runDispatchLoop), so the
// editor's 60 Hz juce::Timer -- which is what feeds LfoDisplay's per-lane phase/value and would
// make the render depend on wall-clock timing -- never actually fires; JUCE dispatches timer
// callbacks through the message queue, not directly from its background timer thread. The
// snapshot is taken immediately after construction, before any timer tick, which is what makes
// two successive renders of unchanged code byte-identical (see docs/ui_migration_report.md for
// the render-twice-and-cmp check this was verified with).

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: lflow_ui_snapshot <out.png> [scale]\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 2.0f;

    // processor declared before editor: C++ destroys locals in reverse declaration order, so
    // the editor is always torn down before the processor it references (spec requirement).
    LFlOwAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::cerr << "createEditor returned null\n";
        return 1;
    }

    if (argc > 4)
    {
        const int w = juce::String (argv[3]).getIntValue();
        const int h = juce::String (argv[4]).getIntValue();
        editor->setSize (w, h); // within setResizeLimits(620, 560, 1000, 900) if given a valid size
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::cerr << "could not write " << out.getFullPathName() << "\n";
        return 1;
    }

    std::cout << out.getFullPathName() << "  " << image.getWidth() << "x" << image.getHeight() << "\n";
    return 0;
}
