import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { LiftPreviewApp } from './LiftPreviewApp';
import '../styles/global.css';

const rootElement = document.getElementById('root');

if (!rootElement) {
  throw new Error('Lift preview root element was not found.');
}

createRoot(rootElement).render(
  <StrictMode>
    <LiftPreviewApp />
  </StrictMode>,
);
