/**
 * Application entry point.
 *
 * Mounts the root React tree into the #root DOM node and imports global
 * Tailwind + custom marble/typography styles.
 */
import { createRoot } from 'react-dom/client';
import App from './App.tsx';
import './index.css';

createRoot(document.getElementById('root')!).render(<App />);
