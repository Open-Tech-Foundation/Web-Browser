import React, { useState, useEffect } from 'react';
import { usePopupRestore } from '../src/components/Popup';
import { nativeRequest } from '../src/shared/nativeRequest';

const WorkspacePopup = () => {
  const [workspaces, setWorkspaces] = useState([]);
  const [renamingId, setRenamingId] = useState(null);
  const [renameDraft, setRenameDraft] = useState('');
  const [renameError, setRenameError] = useState('');
  const [creating, setCreating] = useState(false);
  const [createDraft, setCreateDraft] = useState('');
  const [createError, setCreateError] = useState('');
  const [confirmDeleteId, setConfirmDeleteId] = useState(null);

  usePopupRestore('workspace', (payload) => {
    if (Array.isArray(payload)) {
      setWorkspaces(payload);
      setRenamingId(null);
      setCreating(false);
      setCreateDraft('');
      setCreateError('');
      setRenameError('');
      setConfirmDeleteId(null);
    }
  });

  useEffect(() => {
    const onBlur = () => {
      window.cefQuery?.({ request: 'hide-popup:workspace' });
    };
    const onKeyDown = (e) => {
      if (e.key === 'Escape') {
        window.cefQuery?.({ request: 'hide-popup:workspace' });
      }
    };
    window.addEventListener('blur', onBlur);
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('blur', onBlur);
      window.removeEventListener('keydown', onKeyDown);
    };
  }, []);

  const fetchWorkspaces = () => {
    nativeRequest({ method: 'workspaces.list' })
      .then((list) => setWorkspaces(Array.isArray(list) ? list : []))
      .catch(() => {});
  };

  const submitCreate = () => {
    const name = createDraft.trim();
    if (!name) return;
    setCreateError('');
    nativeRequest({ method: 'workspaces.create', params: { name } })
      .then(() => {
        setCreating(false);
        setCreateDraft('');
        fetchWorkspaces();
      })
      .catch((err) => {
        setCreateError(err.message === 'duplicate name' ? 'Name already in use' : 'Failed to create');
      });
  };

  const cancelCreate = () => {
    setCreating(false);
    setCreateDraft('');
    setCreateError('');
  };

  const submitRename = (id) => {
    const name = renameDraft.trim();
    if (!name) { setRenamingId(null); setRenameDraft(''); setRenameError(''); return; }
    setRenameError('');
    nativeRequest({ method: 'workspaces.rename', params: { id, name } })
      .then(() => {
        setRenamingId(null);
        setRenameDraft('');
        fetchWorkspaces();
      })
      .catch((err) => {
        setRenameError(err.message === 'duplicate name' ? 'Name already in use' : 'Failed to rename');
      });
  };

  const doDelete = (id) => {
    setConfirmDeleteId(null);
    nativeRequest({ method: 'workspaces.delete', params: { id } })
      .then(fetchWorkspaces)
      .catch(() => {});
  };

  return (
    <div className="w-full h-full p-2 bg-transparent box-border">
      <div className="w-full h-full bg-white dark:bg-[#0a0a0c] text-slate-900 dark:text-slate-100 rounded-2xl border border-slate-200 dark:border-white/10 shadow-2xl flex flex-col overflow-hidden">
        <div className="px-3 pt-3 pb-1 text-[10px] font-black uppercase tracking-widest text-slate-400 dark:text-slate-500 shrink-0">
          Workspaces
        </div>
        <div className="flex-1 min-h-0 overflow-y-auto py-1">
          {workspaces.map((w) => (
            <div key={w.id} className="mx-1">
              {confirmDeleteId === w.id ? (
                <div className="flex items-center gap-1 px-2 py-1.5 rounded-lg bg-red-50 dark:bg-red-900/20">
                  <span className="flex-1 text-[11px] text-red-600 dark:text-red-400 truncate">
                    Delete &ldquo;{w.name}&rdquo;?
                  </span>
                  <button
                    onClick={() => doDelete(w.id)}
                    className="px-2 py-0.5 text-[11px] font-semibold bg-red-500 text-white rounded hover:bg-red-600"
                  >
                    Delete
                  </button>
                  <button
                    onClick={() => setConfirmDeleteId(null)}
                    className="px-2 py-0.5 text-[11px] text-slate-500 hover:text-slate-700 dark:hover:text-slate-200 rounded"
                  >
                    Cancel
                  </button>
                </div>
              ) : (
                <div
                  className={`group flex items-center gap-1 px-2 rounded-lg ${
                    w.active ? 'bg-orange-50 dark:bg-orange-500/10' : 'hover:bg-slate-100 dark:hover:bg-white/5'
                  }`}
                >
                  {renamingId === w.id ? (
                    <div className="flex-1 flex flex-col">
                      <input
                        autoFocus
                        value={renameDraft}
                        onChange={(e) => { setRenameDraft(e.target.value); setRenameError(''); }}
                        onKeyDown={(e) => {
                          if (e.key === 'Enter') submitRename(w.id);
                          if (e.key === 'Escape') { setRenamingId(null); setRenameDraft(''); setRenameError(''); }
                        }}
                        onBlur={() => submitRename(w.id)}
                        className="py-1.5 text-[12px] bg-transparent border-b border-slate-300 dark:border-white/20 outline-none focus:border-brand-orange text-slate-700 dark:text-slate-200"
                      />
                      {renameError && (
                        <span className="text-[10px] text-red-500 py-0.5">{renameError}</span>
                      )}
                    </div>
                  ) : (
                    <button
                      onClick={() => nativeRequest({
                        method: 'workspaces.switch',
                        params: { id: w.id },
                      }).catch(() => {})}
                      className={`flex-1 py-1.5 text-left text-[12px] font-medium truncate ${
                        w.active ? 'text-brand-orange' : 'text-slate-700 dark:text-slate-200'
                      }`}
                    >
                      {w.name}
                    </button>
                  )}
                  {renamingId !== w.id && (
                    <div className="flex items-center gap-0.5 opacity-0 group-hover:opacity-100 shrink-0">
                      <button
                        title="Rename"
                        onClick={() => { setRenamingId(w.id); setRenameDraft(w.name); setRenameError(''); }}
                        className="p-1 text-slate-400 hover:text-brand-orange rounded"
                      >
                        <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                          <path d="M12 20h9" />
                          <path d="M16.5 3.5a2.12 2.12 0 1 1 3 3L7 19l-4 1 1-4Z" />
                        </svg>
                      </button>
                      {workspaces.length > 1 && (
                        <button
                          title="Delete"
                          onClick={() => setConfirmDeleteId(w.id)}
                          className="p-1 text-slate-400 hover:text-red-500 rounded"
                        >
                          <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                            <path d="M3 6h18" />
                            <path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
                            <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" />
                          </svg>
                        </button>
                      )}
                    </div>
                  )}
                </div>
              )}
            </div>
          ))}
        </div>
        <div className="border-t border-slate-200 dark:border-white/10 mx-2 shrink-0" />
        {creating ? (
          <div className="p-2 shrink-0 flex flex-col gap-1">
            <input
              autoFocus
              value={createDraft}
              onChange={(e) => { setCreateDraft(e.target.value); setCreateError(''); }}
              onKeyDown={(e) => {
                if (e.key === 'Enter') submitCreate();
                if (e.key === 'Escape') cancelCreate();
              }}
              placeholder="Workspace name"
              className="w-full px-2 py-1.5 text-[12px] bg-transparent border border-slate-300 dark:border-white/20 rounded-lg outline-none focus:border-brand-orange text-slate-700 dark:text-slate-200 placeholder:text-slate-400"
            />
            {createError && (
              <span className="text-[10px] text-red-500 px-1">{createError}</span>
            )}
          </div>
        ) : (
          <button
            onClick={() => setCreating(true)}
            className="flex items-center gap-2 px-3 py-2 text-left text-[12px] text-slate-600 dark:text-slate-300 hover:bg-slate-100 dark:hover:bg-white/5 w-full shrink-0"
          >
            <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
              <path d="M5 12h14" />
              <path d="M12 5v14" />
            </svg>
            New workspace
          </button>
        )}
      </div>
    </div>
  );
};

export default WorkspacePopup;
