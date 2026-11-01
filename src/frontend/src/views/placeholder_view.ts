export class PlaceholderView {
    private canvasContainer: HTMLElement;
    private inspectorPanel: HTMLElement;
    private title: string;
    private subtitle: string;
    private phaseText: string;

    constructor(
        canvasContainer: HTMLElement,
        inspectorPanel: HTMLElement,
        title: string,
        subtitle: string,
        phaseText: string
    ) {
        this.canvasContainer = canvasContainer;
        this.inspectorPanel = inspectorPanel;
        this.title = title;
        this.subtitle = subtitle;
        this.phaseText = phaseText;
    }

    render() {
        this.canvasContainer.innerHTML = `
            <div style="display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100%; gap: 16px; color: var(--muted);">
                <div style="font-size: 48px; opacity: 0.8;">⚙️</div>
                <div style="font-size: 20px; font-weight: 600; color: var(--text);">${this.title}</div>
                <div style="font-size: 13px; max-width: 400px; text-align: center;">${this.subtitle}</div>
                <span class="brand-badge" style="color: var(--cyan); border-color: var(--cyan);">${this.phaseText}</span>
            </div>
        `;

        this.inspectorPanel.innerHTML = `
            <div class="card">
                <div class="card-title">
                    <span>${this.title} Inspector</span>
                </div>
                <div style="font-size: 12px; color: var(--muted); line-height: 1.6;">
                    This view is prepared for upcoming project milestones. Active controls and inspectors will be unlocked in ${this.phaseText}.
                </div>
            </div>
        `;
    }

    destroy() {}
}

