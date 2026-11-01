export interface ToastOptions {
    title: string;
    description: string;
    fix?: string;
    type?: 'error' | 'success' | 'warn';
    durationMs?: number;
}

export class ToastManager {
    private container: HTMLElement;

    constructor() {
        let el = document.getElementById('toast-container');
        if (!el) {
            el = document.createElement('div');
            el.id = 'toast-container';
            el.className = 'toast-container';
            document.body.appendChild(el);
        }
        this.container = el;
    }

    show(options: ToastOptions) {
        const toast = document.createElement('div');
        toast.className = `toast ${options.type || 'error'}`;

        const content = document.createElement('div');
        content.className = 'toast-content';

        const title = document.createElement('div');
        title.className = 'toast-title';
        title.textContent = options.title;
        content.appendChild(title);

        const desc = document.createElement('div');
        desc.className = 'toast-desc';
        desc.textContent = options.description;
        content.appendChild(desc);

        if (options.fix) {
            const fix = document.createElement('div');
            fix.className = 'toast-fix';
            fix.textContent = `Fix: ${options.fix}`;
            content.appendChild(fix);
        }

        toast.appendChild(content);
        this.container.appendChild(toast);

        const timeout = options.durationMs || 5000;
        setTimeout(() => {
            toast.style.opacity = '0';
            toast.style.transform = 'translateY(10px)';
            toast.style.transition = 'all 150ms ease';
            setTimeout(() => toast.remove(), 160);
        }, timeout);
    }
}

export const toast = new ToastManager();

