plot-grid:
    margin: 0px
    size: (fill, fill)
    min-size: (100px, 100px)
    bg-color: @color-surface-dark
    border: 1px @color-border
    radius: 10px

plot-grid-line:
    size: (1px, 1px)
    bg-color: (255, 255, 255, 0.05)

plot-grid-baseline:
    size: (1px, 1px)
    bg-color: (255, 255, 255, 0.35)

plot-grid-label:
    margin: 4px
    size: (fit, fit)
    color: (255, 255, 255, 0.65)
    font-size: 9pt

plot-grid-label-background:
    bg-color: (31, 31, 31)

plot-curve:
    margin: 0px
    padding: 0px
    size: (fill, fill)
    min-size: (100px, 100px)
    bg-color: (31, 31, 31)

plot-curve-line:
    height: 2px
    bg-color: @color-accent

plot-control-curve-line:
    height: 2px
    bg-color: (255, 255, 255, 0.35)

plot-control-curve-point:
    size: (10px, 10px)
    radius: 5px
    bg-color: (230, 230, 230)
    @hover:
        bg-color: (255, 191, 66)
    @active:
        bg-color: (255, 191, 66)

plot-control-curve-handle:
    size: (8px, 8px)
    radius: 4px
    bg-color: (204, 204, 204)
    @hover:
        bg-color: (255, 191, 66)
    @active:
        bg-color: (255, 191, 66)