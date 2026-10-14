// scratch-blocks has no pen blocks (scratch-vm's extension manager supplies them); define them here.
export function definePenBlocks(Blockly) {
    if (Blockly.Blocks.pen_penDown) return;
    const stmt = (type, message0, args0 = []) => ({ type, message0, args0, category: Blockly.Categories.pen, extensions: ['colours_pen', 'shape_statement'] });
    const val = name => ({ type: 'input_value', name });
    Blockly.defineBlocksWithJsonArray([
        stmt('pen_clear', 'erase all'),
        stmt('pen_stamp', 'stamp'),
        stmt('pen_penDown', 'pen down'),
        stmt('pen_penUp', 'pen up'),
        stmt('pen_setPenColorToColor', 'set pen color to %1', [val('COLOR')]),
        stmt('pen_changePenColorParamBy', 'change pen %1 by %2', [val('COLOR_PARAM'), val('VALUE')]),
        stmt('pen_setPenColorParamTo', 'set pen %1 to %2', [val('COLOR_PARAM'), val('VALUE')]),
        stmt('pen_changePenSizeBy', 'change pen size by %1', [val('SIZE')]),
        stmt('pen_setPenSizeTo', 'set pen size to %1', [val('SIZE')]),
        stmt('pen_setPenShadeToNumber', 'set pen shade to %1', [val('SHADE')]),
        stmt('pen_changePenShadeBy', 'change pen shade by %1', [val('SHADE')]),
        stmt('pen_setPenHueToNumber', 'set pen color to %1', [val('HUE')]),
        stmt('pen_changePenHueBy', 'change pen color by %1', [val('HUE')]),
        {
            type: 'pen_menu_colorParam', message0: '%1', category: Blockly.Categories.pen, extensions: ['colours_pen', 'output_string'],
            args0: [{ type: 'field_dropdown', name: 'colorParam', options: [['color', 'color'], ['saturation', 'saturation'], ['brightness', 'brightness'], ['transparency', 'transparency']] }],
        },
    ]);
}
